#include "mainwindow.h"

#include <qlineedit.h>
#include <qaction.h>
#include <qmenubar.h>
#include <qtoolbar.h>
#include <qfiledialog.h>
#include <qstatusbar.h>
#include <qlayout.h>
#include <qapplication.h>

#include "qgeckoembed.h"

const QString rsrcPath = ":/images/lin";

MyMainWindow::MyMainWindow()
    : zoomFactor( 1.0f )
{

    QFrame *box = new QFrame(this);
    qecko = new QGeckoEmbed(box, "qgecko");
    box->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    QHBoxLayout *hboxLayout = new QHBoxLayout(box);
    hboxLayout->addWidget(qecko);
    setCentralWidget( box );

    QToolBar *toolbar = new QToolBar(this);
    toolbar->setWindowTitle("Location:");
    addToolBar(toolbar);
    setToolButtonStyle(Qt::ToolButtonTextOnly);

    QAction *action = new QAction(QIcon(rsrcPath + "/back.png"), tr( "Go Back"), toolbar);
    action->setShortcut(Qt::ControlModifier + Qt::Key_B);
    connect(action, SIGNAL(triggered()), this, SLOT(goBack()));
    toolbar->addAction(action);

    action = new QAction(QIcon(rsrcPath + "/forward.png" ), tr( "Go Forward"), toolbar);
    action->setShortcut(Qt::ControlModifier + Qt::Key_F);
    connect(action, SIGNAL(triggered()), this, SLOT(goForward()));
    toolbar->addAction(action);

    action = new QAction(QIcon(rsrcPath + "/stop.png" ), tr("Stop"), toolbar);
    action->setShortcut(Qt::ControlModifier + Qt::Key_S);
    connect(action, SIGNAL(triggered()), this, SLOT(stop()));
    toolbar->addAction(action);

    action = new QAction(QIcon(rsrcPath + "/stop.png" ), tr("Zoom In"), toolbar);
    action->setShortcut(Qt::ControlModifier + Qt::Key_Plus);
    connect(action, SIGNAL(triggered()), this, SLOT(zoomIn()));
    toolbar->addAction(action);

    action = new QAction(QIcon(rsrcPath + "/stop.png" ), tr("Zoom Out"), toolbar);
    action->setShortcut(Qt::ControlModifier + Qt::Key_Minus);
    connect(action, SIGNAL(triggered()), this, SLOT(zoomOut()));
    toolbar->addAction(action);


    location = new QLineEdit(toolbar);
    toolbar->addWidget(location);

    QMenu *menu = new QMenu(tr( "&File" ), this);
    menuBar()->addMenu( menu );

    QAction *a = new QAction( QIcon(rsrcPath + "/fileopen.png" ), tr( "&Open..." ), toolbar);
    a->setShortcut( Qt::ControlModifier + Qt::Key_O );
    connect( a, SIGNAL( triggered() ), this, SLOT( fileOpen() ) );
    menu->addAction(a);


    connect( qecko, SIGNAL(linkMessage(const QString &)),
             statusBar(), SLOT(message(const QString &)) );
    connect( qecko, SIGNAL(jsStatusMessage(const QString &)),
             statusBar(), SLOT(message(const QString &)) );
    connect( qecko, SIGNAL(windowTitleChanged(const QString &)),
             SLOT(setCaption(const QString &)) );
    connect( qecko, SIGNAL(startURIOpen(const QString &, bool &)),
             SLOT(startURIOpen(const QString &, bool &)) );
    connect(qecko, SIGNAL(locationChanged(const QString&)),
            location, SLOT(setText(const QString&)));
    connect(qecko, SIGNAL(progress(int, int)),
            SLOT(slotProgress(int, int)));
    connect(qecko, SIGNAL(progressAll(const QString&, int, int)),
            SLOT(slotProgress(const QString&, int, int)));
    connect(qecko, SIGNAL(newWindow(QGeckoEmbed**, int)),
            SLOT(slotNewWindow(QGeckoEmbed**, int)));

    connect( location, SIGNAL(returnPressed()), SLOT(changeLocation()));

    connect( qApp, SIGNAL(lastWindowClosed()), SLOT(mainQuit()));
}

void MyMainWindow::mainQuit()
{
    delete qecko;
    qecko = NULL;
}

void MyMainWindow::fileOpen()
{
    QString fn = QFileDialog::getOpenFileName( this, tr( "HTML-Files (*.htm *.html);;All Files (*)" ), QDir::currentPath());
    if ( !fn.isEmpty() )
        qecko->loadURL( fn );
}

void MyMainWindow::startURIOpen(const QString &, bool &)
{
    qDebug("XX in the signal");
}

void MyMainWindow::changeLocation()
{
    qecko->loadURL( location->text() );
}

void MyMainWindow::goBack()
{
    qecko->goBack();
}

void MyMainWindow::goForward()
{
    qecko->goForward();
}

void MyMainWindow::stop()
{
    qecko->stopLoad();
}

void MyMainWindow::zoomIn()
{
    zoomFactor += 0.2f;
    qecko->zoom( zoomFactor ); 
}

void MyMainWindow::zoomOut()
{
    zoomFactor -= 0.2f; 
    qecko->zoom( zoomFactor ); 
}

void MyMainWindow::slotProgress(const QString &url, int current, int max)
{
    qDebug("progress %d / %d (%s)",  current, max, url.toUtf8().data());
}

void MyMainWindow::slotProgress(int current, int max)
{
    qDebug("progress %d / %d ", current, max);
}

void MyMainWindow::slotNewWindow(QGeckoEmbed **newWindow, int chromeMask)
{
    MyMainWindow *mainWindow = new MyMainWindow();
    if(!mainWindow) return;
    mainWindow->resize(400, 600);
    mainWindow->show();

    *newWindow = mainWindow->qecko;
}
