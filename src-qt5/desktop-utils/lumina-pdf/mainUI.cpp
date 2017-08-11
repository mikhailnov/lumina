//===========================================
//  Lumina Desktop source code
//  Copyright (c) 2017, Ken Moore
//  Available under the 3-clause BSD license
//  See the LICENSE file for full details
//===========================================
#include "mainUI.h"
#include "ui_mainUI.h"

#include <QPainter>
#include <QImage>
#include <QSize>
#include <QFileDialog>
#include <QInputDialog>
#include <QDebug>
#include <QApplication>
#include <QScreen>
#include <QTimer>

#include <QtConcurrent>

#include <LuminaXDG.h>

MainUI::MainUI() : QMainWindow(), ui(new Ui::MainUI()){
  ui->setupUi(this);
  presentationLabel = 0;
  this->setWindowTitle(tr("Lumina PDF Viewer"));
  this->setWindowIcon( LXDG::findIcon("application-pdf","unknown"));
  CurrentPage = 0;
  lastdir = QDir::homePath();
  Printer = new QPrinter();
  WIDGET = new QPrintPreviewWidget(Printer,this);
  this->setCentralWidget(WIDGET);
  connect(WIDGET, SIGNAL(paintRequested(QPrinter*)), this, SLOT(paintOnWidget(QPrinter*)) );
  DOC = 0;
  connect(this, SIGNAL(PageLoaded(int)), this, SLOT(slotPageLoaded(int)) );

  PrintDLG = new QPrintDialog(this);
  connect(PrintDLG, SIGNAL(accepted(QPrinter*)), this, SLOT(paintToPrinter(QPrinter*)) );
  connect(ui->menuStart_Presentation, SIGNAL(triggered(QAction*)), this, SLOT(slotStartPresentation(QAction*)) );

  //Create the other interface widgets
  progress = new QProgressBar(this);
    progress->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    progress->setFormat("%v/%m (%p%)"); // [current]/[total]
  progAct = ui->toolBar->addWidget(progress);
  progAct->setVisible(false);
  //Put the various actions into logical groups
  QActionGroup *tmp = new QActionGroup(this);
    tmp->setExclusive(true);
    tmp->addAction(ui->actionFit_Width);
    tmp->addAction(ui->actionFit_Page);
  ui->actionFit_Page->setChecked(true);

  tmp = new QActionGroup(this);
    tmp->setExclusive(true);
    tmp->addAction(ui->actionSingle_Page);
    tmp->addAction(ui->actionDual_Pages);
    tmp->addAction(ui->actionAll_Pages);
  ui->actionSingle_Page->setChecked(true);

  //Connect up the buttons
  connect(ui->actionClose, SIGNAL(triggered()), this, SLOT(close()) );
  connect(ui->actionPrint, SIGNAL(triggered()), PrintDLG, SLOT(open()) );
  connect(ui->actionFit_Width, SIGNAL(triggered()), WIDGET, SLOT(fitToWidth()) );
  connect(ui->actionFit_Page, SIGNAL(triggered()), WIDGET, SLOT(fitInView()) );
  connect(ui->actionOpen_PDF, SIGNAL(triggered()), this, SLOT(OpenNewFile()) );
  connect(ui->actionSingle_Page, SIGNAL(triggered()), WIDGET, SLOT(setSinglePageViewMode()) );
  connect(ui->actionDual_Pages, SIGNAL(triggered()), WIDGET, SLOT(setFacingPagesViewMode()) );
  connect(ui->actionAll_Pages, SIGNAL(triggered()), WIDGET, SLOT(setAllPagesViewMode()) );

  //Setup all the icons
  ui->actionPrint->setIcon( LXDG::findIcon("document-print",""));
  ui->actionClose->setIcon( LXDG::findIcon("window-close",""));
  ui->actionFit_Width->setIcon(LXDG::findIcon("arrow-expand-all",""));
  ui->actionFit_Page->setIcon(LXDG::findIcon("zoom-fit-best",""));
  ui->actionOpen_PDF->setIcon(LXDG::findIcon("document-open",""));
  ui->actionSingle_Page->setIcon(LXDG::findIcon("view-preview",""));
  ui->actionDual_Pages->setIcon(LXDG::findIcon("format-view-agenda",""));
  ui->actionAll_Pages->setIcon(LXDG::findIcon("format-view-grid-small",""));
}

MainUI::~MainUI(){

}

void MainUI::loadFile(QString path){

  if(!QFile::exists(path) || path.isEmpty() ){ return; }
  Poppler::Document *TDOC = Poppler::Document::load(path);
  if(TDOC==0){
    qDebug() << "Could not open file:" << path;
    return;
  }else if(TDOC->isLocked()){
    //Prompt for password to unlock the document
    QString pass = "";
    bool ok = true; //flag this to go into the loop the first time (if password prompt is cancelled - this becomes false)
    while( (ok ? true : !TDOC->unlock(QByteArray(), pass.toLocal8Bit())) ){ //make sure the unlock function is only called when ok is true
      pass = QInputDialog::getText(this, tr("Unlock PDF"), tr("Password:"), QLineEdit::Password, "", &ok);
    }
    if(TDOC->isLocked()){ return; } //Cancelled - still locked
  }

  if(DOC!=0){
    //Clear out the old document first
    delete DOC;
    DOC=0;
  }
  loadingHash.clear(); //clear out this hash
  numPages = -1;
  DOC = TDOC; //Save this for later
  qDebug() << "Opening File:" << path;
  this->setWindowTitle(DOC->title());
  if(this->windowTitle().isEmpty()){ this->setWindowTitle(path.section("/",-1)); }

  //Setup the Document
  Poppler::Page *PAGE = DOC->page(0);
  if(PAGE!=0){
    lastdir = path.section("/",0,-2); //save this for later
    Printer->setPageSize( QPageSize(PAGE->pageSize(), QPageSize::Point) );
    Printer->setPageMargins(QMarginsF(0,0,0,0), QPageLayout::Point);
    switch(PAGE->orientation()){
	case Poppler::Page::Landscape:
	  Printer->setOrientation(QPrinter::Landscape); break;
	default:
	  Printer->setOrientation(QPrinter::Portrait);
    }
    delete PAGE;
    qDebug() << " - Document Setup";
    QTimer::singleShot(10, WIDGET, SLOT(updatePreview())); //start loading the file preview
  }

}

void MainUI::loadPage(int num, Poppler::Document *doc, MainUI *obj, QSize dpi, QSizeF page){
  //qDebug() << " - Render Page:" << num;
  Poppler::Page *PAGE = doc->page(num);
  if(PAGE!=0){
    loadingHash.insert(num, PAGE->renderToImage(3*dpi.width(), 3*dpi.height()).scaled(page.width(), page.height(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation) );
  }else{
    loadingHash.insert(num, QImage());
  }
  if(PAGE!=0){ delete PAGE; }
  obj->emit PageLoaded(num);
}

QScreen* MainUI::getScreen(bool current, bool &cancelled){
  //Note: the "cancelled" boolian is actually an output - not an input
  QList<QScreen*> screens = QApplication::screens();
  cancelled = false;
  if(screens.length() ==1){ return screens[0]; } //only one option
  //Multiple screens available - figure it out
  if(current){
    //Just return the screen the window is currently on
    for(int i=0; i<screens.length(); i++){
      if(screens[i]->geometry().contains( this->mapToGlobal(this->pos()) )){
        return screens[i];
      }
    }
    //If it gets this far, there was an error and it should just return the primary screen
    return QApplication::primaryScreen();
  }else{
    //Ask the user to select a screen (for presentations, etc..)
    QStringList names;
    for(int i=0; i<screens.length(); i++){
      QString screensize = QString::number(screens[i]->size().width())+"x"+QString::number(screens[i]->size().height());
       names << QString(tr("%1 (%2)")).arg(screens[i]->name(), screensize);
    }
    bool ok = false;
    QString sel = QInputDialog::getItem(this, tr("Select Screen"), tr("Screen:"), names, 0, false, &ok);
    cancelled = !ok;
    if(!ok){ return screens[0]; } //cancelled - just return the first one
    int index = names.indexOf(sel);
    if(index < 0){ return screens[0]; } //error - should never happen though
    return screens[index]; //return the selected screen
  }
}

void MainUI::startPresentation(bool atStart){
  if(DOC==0){ return; } //just in case
  bool cancelled = false;
  QScreen *screen = getScreen(false, cancelled); //let the user select which screen to use (if multiples)
  if(cancelled){ return;}
  int page = 0;
  if(!atStart){ page = CurrentPage; }
  //PDPI = QSize(SCALEFACTOR*screen->physicalDotsPerInchX(), SCALEFACTOR*screen->physicalDotsPerInchY());
  //Now create the full-screen window on the selected screen
  if(presentationLabel == 0){
    //Create the label and any special flags for it
    presentationLabel = new QLabel(0, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
      presentationLabel->setStyleSheet("background-color: black;");
      presentationLabel->setAlignment(Qt::AlignCenter);
  }
  //Now put the label in the proper location
  presentationLabel->setGeometry(screen->geometry());
  presentationLabel->showFullScreen();

  ui->actionStop_Presentation->setEnabled(true);
  ui->menuStart_Presentation->setEnabled(false);
  QApplication::processEvents();
  //Now start at the proper page
  ShowPage(page);
  this->grabKeyboard(); //Grab any keyboard events - even from the presentation window
}

void MainUI::ShowPage(int page){
  if(presentationLabel == 0 || !presentationLabel->isVisible()){ return; }
  //Check for valid document/page
  if(page<0 || page >=numPages ){
    endPresentation();
    return; //invalid - no document loaded or invalid page specified
  }
  CurrentPage = page;
  QImage PAGEIMAGE = loadingHash[page];

  //Now scale the image according to the user-designations and show it
  if(!PAGEIMAGE.isNull()){
    QPixmap pix;
    pix.convertFromImage( PAGEIMAGE.scaled( presentationLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation) );
    presentationLabel->setPixmap(pix);
    presentationLabel->show(); //always make sure it was not hidden
  }else{
    presentationLabel->setPixmap(QPixmap());
    endPresentation();
  }
}

void MainUI::endPresentation(){
  if(presentationLabel==0 || !presentationLabel->isVisible()){ return; } //not in presentation mode
  presentationLabel->hide(); //just hide this (no need to re-create the label for future presentations)
  ui->actionStop_Presentation->setEnabled(false);
  ui->menuStart_Presentation->setEnabled(true);
  this->releaseKeyboard();
}

void MainUI::startLoadingPages(QPrinter *printer){
  if(numPages>0){ return; } //currently loaded[ing]
  //qDebug() << " - Start Loading Pages";
  numPages = DOC->numPages();
  progress->setRange(0,numPages);
  progress->setValue(0);
  progAct->setVisible(true);
  QRectF pageSize = printer->pageRect(QPrinter::DevicePixel);
  QSize DPI(printer->resolution(),printer->resolution());
  for(int i=0; i<numPages; i++){
    //qDebug() << " - Kickoff page load:" << i;
    QtConcurrent::run(this, &MainUI::loadPage, i, DOC, this, DPI, pageSize.size() );
  }
}

void MainUI::slotPageLoaded(int page){
  //qDebug() << "Page Loaded:" << page;
  int finished = loadingHash.keys().length();
  if(finished == numPages){
    progress->setVisible(false);
    progAct->setVisible(false);
    QTimer::singleShot(0, WIDGET, SLOT(updatePreview()));
  }else{
    progress->setValue(finished);
  }
}

void MainUI::slotStartPresentation(QAction *act){
  startPresentation(act == ui->actionAt_Beginning);
}

void MainUI::paintOnWidget(QPrinter *PRINTER){
  if(DOC==0){ return; }
  //this->show();
  if(loadingHash.keys().length() != numPages){ startLoadingPages(PRINTER); return; }

  QPainter painter(PRINTER);
    for(int i=0; i<numPages; i++){
      if(i != 0){ PRINTER->newPage(); } //this is the start of the next page (not needed for first)
      if(loadingHash.contains(i)){ painter.drawImage(0,0, loadingHash[i]); }
      else{ painter.drawImage(0,0, QImage()); }
    }
}

void MainUI::paintToPrinter(QPrinter *PRINTER){
  if(loadingHash.keys().length() != numPages){ return; }

  int pages = DOC->numPages();
  int firstpage = 0;
  int copies = PRINTER->copyCount();
  bool collate = PRINTER->collateCopies();
  bool duplex = (PRINTER->duplex()!=QPrinter::DuplexNone);
  //Determine the first page that needs to be printed, and the range
  if((PRINTER->fromPage() != PRINTER->toPage() || PRINTER->fromPage()!=0 ) && PRINTER->printRange()==QPrinter::PageRange ){
    firstpage = PRINTER->fromPage() - 1;
    pages = PRINTER->toPage();
  }
  qDebug() << "Start Printing PDF: Pages" << PRINTER->fromPage() <<" to "<< PRINTER->toPage()<< " Copies:" << copies << "  collate:" << collate;
  QList<int> pageCount;
  //Assemble the page order/count based on printer settings
  for(int i=firstpage; i<pages; i++){
    //Make sure even/odd pages are not selected as desired
    //Qt 5.7.1 does not seem to have even/odd page selections - 8/11/2017
    pageCount << i; //add this page to the list
    for(int c=1; c<copies && !collate; c++){ pageCount << i; } //add any copies of this page as needed
  }
  //qDebug() << "Got Page Range:" << pageCount;
  if(PRINTER->pageOrder()==QPrinter::LastPageFirst){
    //Need to reverse the order of the list
    QList<int> tmp = pageCount;
    pageCount.clear();
    for(int i=tmp.length()-1; i>=0; i--){ pageCount << tmp[i]; }
    //qDebug() << " - reversed:" << pageCount;
  }
  if(collate && copies>0){
    QList<int> orig = pageCount; //original array of pages
    for(int c=1; c<copies; c++){
      pageCount << orig; //add a new copy of the entire page range
    }
  }
  //qDebug() << "Final Page Range:" << pageCount;
  //return;
  //Generate the sizing information for the printer
  QSize sz(PRINTER->pageRect().width(), PRINTER->pageRect().height());
  bool landscape = PRINTER->orientation()==QPrinter::Landscape;
  if(landscape){ sz = QSize(sz.height(), sz.width() ); } //flip the size dimensions as needed
  //Now send out the pages in the right order/format
  QPainter painter(PRINTER);
  QTransform transF;
  transF.rotate(90);
  for(int i=0; i<pageCount.length(); i++){
    if(i!=0){ PRINTER->newPage(); }
    QImage img = loadingHash[pageCount[i]].scaled(sz, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    qDebug() << "Image Size:" << img.size();
    //if(landscape){ img = img.transformed(transF, Qt::SmoothTransformation); qDebug() << " - rotated image size:" << img.size(); }
    //Now draw the image
    painter.drawImage(0,0,img);
  }
}

void MainUI::OpenNewFile(){
  //Prompt for a file
  QString path = QFileDialog::getOpenFileName(this, tr("Open PDF"), lastdir, tr("PDF Documents (*.pdf)"));
  //Now Open it
  if(!path.isEmpty()){ loadFile(path); }
}
