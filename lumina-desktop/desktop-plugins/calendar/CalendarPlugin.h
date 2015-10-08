//===========================================
//  Lumina-DE source code
//  Copyright (c) 2014, Ken Moore
//  Available under the 3-clause BSD license
//  See the LICENSE file for full details
//===========================================
//  This class is a quick sample desktop plugin
//===========================================
#ifndef _LUMINA_DESKTOP_DESKTOP_PLUGIN_CALENDAR_H
#define _LUMINA_DESKTOP_DESKTOP_PLUGIN_CALENDAR_H

#include <QCalendarWidget>
#include <QVBoxLayout>
#include <QDate>
#include <QTimer>
#include "../LDPlugin.h"

class CalendarPlugin : public LDPlugin{
	Q_OBJECT
private:
	QCalendarWidget *cal;
	QTimer *timer;

public:
	CalendarPlugin(QWidget* parent, QString ID) : LDPlugin(parent, ID){
	  this->setLayout( new QVBoxLayout());
	    this->layout()->setContentsMargins(0,0,0,0);
	  cal = new QCalendarWidget(this);
	  cal->setSelectionMode(QCalendarWidget::NoSelection);
	  this->layout()->addWidget(cal);
	  this->setInitialSize( cal->sizeHint().width(), cal->sizeHint().height() );
	  timer = new QTimer(this);
	    timer->setInterval(1800000); //30 minute refresh timer
	    timer->start();
	  QTimer::singleShot(0,this, SLOT(updateDate()) );
	}
	
	~CalendarPlugin(){ timer->stop(); }
	
private slots:
	void updateDate(){
	  if(cal->selectedDate() != QDate::currentDate()){
	    cal->setSelectedDate(QDate::currentDate());
	    cal->showSelectedDate();
	  }
	}
	

};
#endif
