/*
 * This file is part of the xTuple ERP: PostBooks Edition, a free and
 * open source Enterprise Resource Planning software suite,
 * Copyright (c) 1999-2015 by OpenMFG LLC, d/b/a xTuple.
 * It is licensed to you under the Common Public Attribution License
 * version 1.0, the full text of which (including xTuple-specific Exhibits)
 * is available at www.xtuple.com/CPAL.  By using this software, you agree
 * to be bound by its terms.
 */

#include "miscCheck.h"

#include <QMessageBox>
#include <QSqlError>
#include <QVariant>
#include <metasql.h>
#include "mqlutil.h"
#include "errorReporter.h"
#include "storedProcErrorLookup.h"

miscCheck::miscCheck(QWidget* parent, const char* name, Qt::WindowFlags fl)
    : XWidget(parent, name, fl)
{
  setupUi(this);

  connect(_bankaccnt, SIGNAL(newID(int)),    this, SLOT(sPopulateBankInfo(int)));
  connect(_cust,      SIGNAL(newId(int)),    this, SLOT(sHandleButtons()));
  connect(_cust,      SIGNAL(newId(int)),    this, SLOT(sHandleTaxZone()));
  connect(_custRB,    SIGNAL(toggled(bool)), this, SLOT(sHandleButtons()));
  connect(_save,      SIGNAL(clicked()),     this, SLOT(sSave()));
  connect(_taxauth,   SIGNAL(newID(int)),    this, SLOT(sHandleButtons()));
  connect(_taxauthRB, SIGNAL(toggled(bool)), this, SLOT(sHandleButtons()));
  connect(_vend,      SIGNAL(newId(int)),    this, SLOT(sHandleButtons()));
  connect(_vend,      SIGNAL(newId(int)),    this, SLOT(sHandleTaxZone()));
  connect(_vendRB,    SIGNAL(toggled(bool)), this, SLOT(sHandleButtons()));
  connect(_cust,      SIGNAL(valid(bool)),   this, SLOT(sCustomerSelected()));
  connect(_expense,   SIGNAL(toggled(bool)), this, SLOT(sHandleButtons()));
  connect(_amount,    SIGNAL(editingFinished()), this, SLOT(sCalculateTax()));
  connect(_taxzone,   SIGNAL(newID(int)),     this, SLOT(sCalculateTax()));
  connect(_taxtype,   SIGNAL(newID(int)),     this, SLOT(sCalculateTax()));

  _captive = false;
  _raheadid = -1;
  _aropenamt = 0;

  _bankaccnt->setType(XComboBox::APBankAccounts);
}

miscCheck::~miscCheck()
{
  // no need to delete child widgets, Qt does it all for us
}

void miscCheck::languageChange()
{
  retranslateUi(this);
}

enum SetResponse miscCheck::set(const ParameterList &pParams)
{
  XWidget::set(pParams);
  _captive = true;

  QVariant param;
  bool     valid;

  param = pParams.value("vend_id", &valid);
  if (valid)
    _vend->setId(param.toInt());

  param = pParams.value("bankaccnt_id", &valid);
  if (valid)
    _bankaccnt->setId(param.toInt());
   
  param = pParams.value("check_id", &valid);
  if (valid)
  {
    connect(_cmCluster, SIGNAL(newId(int)),   this, SLOT(sCreditMemoSelected()));
    _checkid = param.toInt();
    populate();
  }

  if (pParams.inList("new"))
  {
    _mode = cNew;

    _save->setText(tr("C&reate"));

    connect(_cmCluster, SIGNAL(newId(int)),   this, SLOT(sCreditMemoSelected()));
  }
  else if (pParams.inList("edit"))
  {
    _mode = cEdit;

    _bankaccnt->setEnabled(false);
  }

  return NoError;
}

void miscCheck::sSave()
{
  double _amt;
  _amt=_amount->localValue();
  XSqlQuery check;
  if (!_date->isValid())
  {
    QMessageBox::warning( this, tr("Cannot Create Miscellaneous Payment"),
                          tr("<p>You must enter a date for this payment.") );
    _date->setFocus();
    return;
  }
  
  if (_amount->isZero())
  {
    QMessageBox::warning( this, tr("Cannot Create Miscellaneous Payment"),
                          tr("<p>You must enter an amount for this payment.") );
    _date->setFocus();
    return;
  }

  if ( (_applytocm->isChecked()) && (_cmCluster->id() == -1) )
  {
    QMessageBox::warning( this, tr("Cannot Create Miscellaneous Payment"),
                          tr("<p>You must select a Credit Memo for this "
			     "expensed payment.") );
    _expcat->setFocus();
    return;
  }
 
  if (_applytocm->isChecked() && _cmCluster->isValid() && (_amt > _aropenamt))
  {
    QMessageBox::warning( this, tr("Invalid Amount"),
                            tr("<p>You must enter an amount less than or equal to the  "
	   		                   "credit memo selected.") );
    _amount->setLocalValue(_aropenamt);
	_amount->setFocus();
	return;
  }
 
  if ( (_expense->isChecked()) && (_expcat->id() == -1) )
  {
    QMessageBox::warning( this, tr("Cannot Create Miscellaneous Payment"),
                          tr("<p>You must select an Expense Category for this "
			     "expensed payment.") );
    _expcat->setFocus();
    return;
  }
 
  if (_mode == cNew)
  {
    check.prepare("SELECT createCheck(:bankaccnt_id, :reciptype, :recipid,"
	      "                   :checkDate, :amount, :curr_id, :expcat_id,"
		  "                   NULL, :for, :notes, true, :cmhead_id, :taxzone, :taxtype) AS result;" );
    check.bindValue(":bankaccnt_id", _bankaccnt->id());
  }
  else if (_mode == cEdit)
  {
    check.prepare( "UPDATE checkhead "
               "SET checkhead_recip_id=:recipid,"
	       "    checkhead_recip_type=:reciptype,"
	       "    checkhead_checkdate=:checkDate, "
	       "    checkhead_amount=:amount, checkhead_curr_id=:curr_id, "
               "    checkhead_expcat_id=:expcat_id, checkhead_for=:for,"
	       "    checkhead_notes=:notes, "
               "    checkhead_taxzone_id=:taxzone, "
               "    checkhead_taxtype_id=:taxtype "
               "WHERE (checkhead_id=:check_id);" );
    check.bindValue(":check_id", _checkid);
  }

  if (_vendRB->isChecked())
  {
    check.bindValue(":reciptype",	"V");
    check.bindValue(":recipid",	_vend->id());
  }
  else if (_custRB->isChecked())
  {
    check.bindValue(":reciptype",	"C");
    check.bindValue(":recipid",	_cust->id());
  }
  else if (_taxauthRB->isChecked())
  {
    check.bindValue(":reciptype",	"T");
    check.bindValue(":recipid",	_taxauth->id());
  }
  check.bindValue(":checkDate",	_date->date());
  check.bindValue(":amount",	_amt);
  check.bindValue(":curr_id",	_amount->id());
  if (_taxzone->isValid())
    check.bindValue(":taxzone",	_taxzone->id());
  if (_taxtype->isValid())
    check.bindValue(":taxtype",	_taxtype->id());
  check.bindValue(":for",	_for->text().trimmed());
  check.bindValue(":notes",	_notes->toPlainText().trimmed());

  if (_expense->isChecked())
    check.bindValue(":expcat_id", _expcat->id());

  if ( (_applytocm->isChecked()) && (_cmCluster->isValid()) )
	check.bindValue(":cmhead_id",_cmCluster->id());

  check.exec();
  if (_mode == cNew && check.first())
  {
    _checkid = check.value("result").toInt();
    if (_checkid < 0)
    {
      ErrorReporter::error(QtCriticalMsg, this, tr("Error Saving Check Information"),
                             storedProcErrorLookup("createCheck", _checkid),
                             __FILE__, __LINE__);
      return;
    }
    check.prepare( "SELECT checkhead_number "
               "FROM checkhead "
               "WHERE (checkhead_id=:check_id);" );
    check.bindValue(":check_id", _checkid);
    check.exec();
    if (check.first())
    {
// Since we are no longer assigning check number at creation this is not needed
/*
      QMessageBox::information( this, tr("New Check Created"),
                                tr("<p>A new Check has been created and "
				   "assigned #%1")
                                .arg(check.value("checkhead_number").toString()) );
*/
    }
    else if (ErrorReporter::error(QtCriticalMsg, this, tr("Error Saving Check Information"),
                                  check, __FILE__, __LINE__))
    {
      return;
    }
  }
  else if (ErrorReporter::error(QtCriticalMsg, this, tr("Error Saving Check Information"),
                                check, __FILE__, __LINE__))
  {
    return;
  }

  omfgThis->sChecksUpdated(_bankaccnt->id(), _checkid, true);

  if (_captive)
    close();
}

void miscCheck::sPopulateBankInfo(int pBankaccntid)
{
  XSqlQuery miscPopulateBankInfo;
  if ( pBankaccntid != -1 )
  {
    XSqlQuery checkNumber;
    checkNumber.prepare( "SELECT bankaccnt_nextchknum, bankaccnt_curr_id "
			 "FROM bankaccnt "
			 "WHERE (bankaccnt_id=:bankaccnt_id);" );
    checkNumber.bindValue(":bankaccnt_id", pBankaccntid);
    checkNumber.exec();
    if (checkNumber.first())
    {
      _amount->setId(checkNumber.value("bankaccnt_curr_id").toInt());
    }
    else if (ErrorReporter::error(QtCriticalMsg, this, tr("Error Retrieving Bank Information"),
                                  checkNumber, __FILE__, __LINE__))
    {
      return;
    }
  }

  if (_cmCluster->isValid())
    sCreditMemoSelected();
}

void miscCheck::populate()
{
  XSqlQuery miscpopulate;
  miscpopulate.prepare( "SELECT checkhead_recip_type, checkhead_recip_id,"
	     "       checkhead_bankaccnt_id, checkhead_number,"
             "       checkhead_checkdate, checkhead_expcat_id,"
             "       checkhead_for, checkhead_notes,"
             "       checkhead_amount, checkhead_curr_id, aropen_id AS checkhead_cmhead_id, "
             "       checkhead_taxzone_id, checkhead_taxtype_id "
             "FROM checkhead "
             "  LEFT OUTER JOIN checkitem ON (checkhead_id=checkitem_checkhead_id) "
             "                            AND (checkitem_aropen_id IS NOT NULL) "
             "  LEFT OUTER JOIN aropen ON (checkitem_aropen_id=aropen_id) "
             "                         AND (aropen_doctype = 'C') "
             "WHERE checkhead_id=:check_id;");
  miscpopulate.bindValue(":check_id", _checkid);
  miscpopulate.exec();
  if (miscpopulate.first())
  {
    if (miscpopulate.value("checkhead_recip_type").toString() == "V")
    {
      _vendRB->setChecked(true);
      _vend->setId(miscpopulate.value("checkhead_recip_id").toInt());
    }
    else if (miscpopulate.value("checkhead_recip_type").toString() == "C")
    {
      _custRB->setChecked(true);
      _cust->setId(miscpopulate.value("checkhead_recip_id").toInt());
    }
    else if (miscpopulate.value("checkhead_recip_type").toString() == "T")
    {
      _taxauthRB->setChecked(true);
      _taxauth->setId(miscpopulate.value("checkhead_recip_id").toInt());
    }
    // bank accnt must be set before check number and currency
    _bankaccnt->setId(miscpopulate.value("checkhead_bankaccnt_id").toInt());
    _date->setDate(miscpopulate.value("checkhead_checkdate").toDate(), true);
    _amount->set(miscpopulate.value("checkhead_amount").toDouble(),
		 miscpopulate.value("checkhead_curr_id").toInt(),
		 miscpopulate.value("checkhead_checkdate").toDate(), false);
    _for->setText(miscpopulate.value("checkhead_for"));
    _notes->setText(miscpopulate.value("checkhead_notes").toString());
    _taxzone->setId(miscpopulate.value("checkhead_taxzone_id").toInt());
    _taxtype->setId(miscpopulate.value("checkhead_taxtype_id").toInt());
    sCalculateTax();

    if (!miscpopulate.value("checkhead_cmhead_id").isNull())
    {
      _applytocm->setChecked(true);
      _cmCluster->setId(miscpopulate.value("checkhead_cmhead_id").toInt());
    }
    else if (miscpopulate.value("checkhead_expcat_id").isNull() ||
	miscpopulate.value("checkhead_expcat_id").toInt() == -1)
      _memo->setChecked(true);
    else
    {
      _expense->setChecked(true);
      _expcat->setId(miscpopulate.value("checkhead_expcat_id").toInt());
    }

    if (!miscpopulate.value("checkhead_cmhead_id").isNull())
    {
      _recipGroup->setEnabled(false);
      _chargeToGroup->setEnabled(false);
      _amount->setEnabled(false);
      _for->setEnabled(false);
    }
  }
}

void miscCheck::sHandleButtons()
{
  if (_vendRB->isChecked())
  {
    _widgetStack->setCurrentIndex(0);
    _memo->setText("Create Credit Memo");
    _cust->setId(-1);
  }
  else if (_custRB->isChecked())
  {
    _widgetStack->setCurrentIndex(1);
    _memo->setText("Create Debit Memo");
    _cmCluster->setId(-1);
    _cmCluster->setCustId(_cust->id());
    _vend->setId(-1);
  }
  else
  {
    _widgetStack->setCurrentIndex(2);
    _memo->setText("Create Credit Memo");
    _cust->setId(-1);
    _vend->setId(-1);
  }

  _taxGroup->setVisible(_custRB->isChecked() || _vendRB->isChecked());
  _taxGroup->setEnabled(_expense->isChecked());

  _save->setEnabled((_cust->isValid() && _custRB->isChecked())
	             || (_vend->isValid()  && _vendRB->isChecked()) 
				 || (_taxauth->id() > 0 && _taxauthRB->isChecked()));
}

void miscCheck::sHandleTaxZone()
{
  XSqlQuery getTaxZone;
  QString sql = "<? if exists('cust_id') ?> SELECT cust_taxzone_id AS taxzone_id FROM custinfo "
                "                           WHERE (cust_id = <? value('cust_id') ?>) "
                "<? elseif exists('vend_id') ?> SELECT vend_taxzone_id AS taxzone_id FROM vendinfo "
                "                           WHERE (vend_id = <? value('vend_id') ?>) "
                "<? else ?> SELECT -1 AS taxzone_id "
                "<? endif ?>;";
  ParameterList params;
  if (_cust->isValid())
    params.append("cust_id", _cust->id());
  else if (_vend->isValid())
    params.append("vend_id", _vend->id());
  MetaSQLQuery mql(sql);
  getTaxZone = mql.toQuery(params);
  if (getTaxZone.first())
    _taxzone->setId(getTaxZone.value("taxzone_id").toInt());
}

void miscCheck::sCustomerSelected()
{
  _cmCluster->setEnabled(_cust->isValid() && _applytocm->isChecked());
}

void miscCheck::sCreditMemoSelected()
{
  XSqlQuery miscCreditMemoSelected;
  if (_cmCluster->id() != -1) 
  {  
	if(!_date->isValid())
	  _date->setDate(QDate::currentDate());
	miscCreditMemoSelected.prepare("SELECT aropen_curr_id, "
			  "       round((aropen_amount-aropen_paid- "
			  //Subtract amount for existing checks
			  "(SELECT COALESCE(SUM(checkhead_amount),0) "
			  " FROM checkhead,checkitem "
			  " WHERE ((checkhead_id=checkitem_checkhead_id) "
			  " AND (NOT checkhead_posted) "
			  " AND (NOT checkhead_void) "
                          " AND (checkhead_id <> :check_id) "
			  " AND (checkitem_aropen_id=aropen_id))) "

			  ") * aropen_curr_rate / currRate(:curr_id,aropen_docdate),2) AS amount "
              "  FROM aropen "
              " WHERE (aropen_id=:aropen_id); ");
    miscCreditMemoSelected.bindValue(":aropen_id", _cmCluster->id());
    miscCreditMemoSelected.bindValue(":curr_id", _amount->id());
    miscCreditMemoSelected.bindValue(":date", _date->date());
    miscCreditMemoSelected.bindValue(":check_id", _checkid);
    miscCreditMemoSelected.exec();
    if (miscCreditMemoSelected.first())
    {
      _aropenamt=miscCreditMemoSelected.value("amount").toDouble();
      if (_mode == cNew)
        _amount->setLocalValue(miscCreditMemoSelected.value("amount").toDouble());
    }
    else if (ErrorReporter::error(QtCriticalMsg, this, tr("Error Retrieving Check Information"),
                                  miscCreditMemoSelected, __FILE__, __LINE__))
    {
      return;
    }
  }
  else
  {
    _aropenamt=0;
  } 
}

void miscCheck::sCalculateTax()
{
  if(!_taxzone->isValid() || !_taxtype->isValid())
  {
    _tax->setLocalValue(0.00);
    return;
  }

  XSqlQuery calcTax;
  calcTax.prepare("SELECT calculateinversetax(:taxzone_id, :taxtype_id, :date, :curr_id, :amount) as tax;");
  calcTax.bindValue(":taxzone_id", _taxzone->id());
  calcTax.bindValue(":taxtype_id", _taxtype->id());
  calcTax.bindValue(":date",       _date->date());
  calcTax.bindValue(":curr_id",    _amount->id());
  calcTax.bindValue(":amount",     _amount->localValue());
  calcTax.exec();
  if (calcTax.first())
    _tax->setLocalValue(calcTax.value("tax").toDouble());
  else if (ErrorReporter::error(QtCriticalMsg, this,
                                tr("Tax Calculation"),
                                calcTax, __FILE__, __LINE__))
    return; 
}
