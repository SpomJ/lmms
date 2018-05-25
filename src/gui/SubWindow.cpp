/*
 * SubWindow.cpp - Implementation of QMdiSubWindow that correctly tracks
 *   the geometry that windows should be restored to.
 *   Workaround for https://bugreports.qt.io/browse/QTBUG-256
 *   This implementation adds a custom themed title bar to
 *   the subwindow.
 *
 * Copyright (c) 2015 Colin Wallace <wallace.colin.a@gmail.com>
 * Copyright (c) 2016 Steffen Baranowsky <baramgb@freenet.de>
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "SubWindow.h"

#include <QMdiArea>
#include <QMetaMethod>
#include <QMoveEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWindow>

#include "embed.h"



SubWindow::SubWindow( QWidget *parent, Qt::WindowFlags windowFlags ) :
	QMdiSubWindow( parent, windowFlags ),
	m_buttonSize( 17, 17 ),
	m_titleBarHeight( 24 ),
	m_hasFocus( false )
{
	// initialize the tracked geometry to whatever Qt thinks the normal geometry currently is.
	// this should always work, since QMdiSubWindows will not start as maximized
	m_trackedNormalGeom = normalGeometry();

	// inits the colors
	m_activeColor = Qt::SolidPattern;
	m_textShadowColor = Qt::black;
	m_borderColor = Qt::black;

	// close, maximize and restore (after maximizing) buttons
	m_closeBtn = new QPushButton( embed::getIconPixmap( "close" ), QString(), this );
	m_closeBtn->resize( m_buttonSize );
	m_closeBtn->setFocusPolicy( Qt::NoFocus );
	m_closeBtn->setCursor( Qt::ArrowCursor );
	m_closeBtn->setAttribute( Qt::WA_NoMousePropagation );
	m_closeBtn->setToolTip( tr( "Close" ) );
	connect( m_closeBtn, SIGNAL( clicked( bool ) ), this, SLOT( close() ) );

	m_maximizeBtn = new QPushButton( embed::getIconPixmap( "maximize" ), QString(), this );
	m_maximizeBtn->resize( m_buttonSize );
	m_maximizeBtn->setFocusPolicy( Qt::NoFocus );
	m_maximizeBtn->setCursor( Qt::ArrowCursor );
	m_maximizeBtn->setAttribute( Qt::WA_NoMousePropagation );
	m_maximizeBtn->setToolTip( tr( "Maximize" ) );
	connect( m_maximizeBtn, SIGNAL( clicked( bool ) ), this, SLOT( showMaximized() ) );

	m_restoreBtn = new QPushButton( embed::getIconPixmap( "restore" ), QString(), this );
	m_restoreBtn->resize( m_buttonSize );
	m_restoreBtn->setFocusPolicy( Qt::NoFocus );
	m_restoreBtn->setCursor( Qt::ArrowCursor );
	m_restoreBtn->setAttribute( Qt::WA_NoMousePropagation );
	m_restoreBtn->setToolTip( tr( "Restore" ) );
	connect( m_restoreBtn, SIGNAL( clicked( bool ) ), this, SLOT( showNormal() ) );

	m_detachBtn = new QPushButton( embed::getIconPixmap( "window" ), QString(), this );
	m_detachBtn->resize( m_buttonSize );
	m_detachBtn->setFocusPolicy( Qt::NoFocus );
	m_detachBtn->setCursor( Qt::ArrowCursor );
	m_detachBtn->setAttribute( Qt::WA_NoMousePropagation );
	m_detachBtn->setToolTip( tr( "Detach" ) );
	connect( m_detachBtn, SIGNAL( clicked( bool ) ), this, SLOT( detach() ) );

	// QLabel for the window title and the shadow effect
	m_shadow = new QGraphicsDropShadowEffect();
	m_shadow->setColor( m_textShadowColor );
	m_shadow->setXOffset( 1 );
	m_shadow->setYOffset( 1 );

	m_windowTitle = new QLabel( this );
	m_windowTitle->setFocusPolicy( Qt::NoFocus );
	m_windowTitle->setAttribute( Qt::WA_TransparentForMouseEvents, true );
	m_windowTitle->setGraphicsEffect( m_shadow );

	// disable the minimize button
	setWindowFlags( Qt::SubWindow | Qt::WindowMaximizeButtonHint |
		Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint |
		Qt::CustomizeWindowHint );
	connect( mdiArea(), SIGNAL( subWindowActivated( QMdiSubWindow* ) ), this, SLOT( focusChanged( QMdiSubWindow* ) ) );
}




/**
 * @brief SubWindow::paintEvent
 * 
 *  This draws our new title bar with custom colors
 *  and draws a window icon on the left upper corner.
 */
void SubWindow::paintEvent( QPaintEvent * )
{
	QPainter p( this );
	QRect rect( 0, 0, width(), m_titleBarHeight );

	bool isActive = mdiArea()
			? mdiArea()->activeSubWindow() == this
			: false;

	p.fillRect( rect, isActive ? activeColor() : p.pen().brush() );

	// window border
	p.setPen( borderColor() );

	// bottom, left, and right lines
	p.drawLine( 0, height() - 1, width(), height() - 1 );
	p.drawLine( 0, m_titleBarHeight, 0, height() - 1 );
	p.drawLine( width() - 1, m_titleBarHeight, width() - 1, height() - 1 );

	// window icon
	if( widget() )
	{
		QPixmap winicon( widget()->windowIcon().pixmap( m_buttonSize ) );
		p.drawPixmap( 3, 3, m_buttonSize.width(), m_buttonSize.height(), winicon );
	}
}




/**
 * @brief SubWindow::changeEvent
 * 
 * Triggers if the window title changes and calls adjustTitleBar().
 * @param event
 */
void SubWindow::changeEvent( QEvent *event )
{
	QMdiSubWindow::changeEvent( event );

	if( event->type() == QEvent::WindowTitleChange )
	{
		adjustTitleBar();
	}

}

void SubWindow::showEvent(QShowEvent *e)
{
	attach();
	QMdiSubWindow::showEvent(e);
}

bool SubWindow::isDetached() const
{
	return widget()->windowFlags().testFlag(Qt::Window);
}




/**
 * @brief SubWindow::elideText
 * 
 *  Stores the given text into the given label.
 *  Shorts the text if it's too big for the labels width
 *  ans adds three dots (...)
 * 
 * @param label - holds a pointer to the QLabel
 * @param text  - the text which will be stored (and if needed breaked down) into the QLabel.
 */
void SubWindow::elideText( QLabel *label, QString text )
{
	QFontMetrics metrix( label->font() );
	int width = label->width() - 2;
	QString clippedText = metrix.elidedText( text, Qt::ElideRight, width );
	label->setText( clippedText );
}




/**
 * @brief SubWindow::getTrueNormalGeometry
 * 
 *  same as QWidet::normalGeometry, but works properly under X11
 *  see https://bugreports.qt.io/browse/QTBUG-256
 */
QRect SubWindow::getTrueNormalGeometry() const
{
	return m_trackedNormalGeom;
}




QBrush SubWindow::activeColor() const
{
	return m_activeColor;
}




QColor SubWindow::textShadowColor() const
{
	return m_textShadowColor;
}




QColor SubWindow::borderColor() const
{
	return m_borderColor;
}




void SubWindow::setActiveColor( const QBrush & b )
{
	m_activeColor = b;
}




void SubWindow::setTextShadowColor( const QColor & c )
{
	m_textShadowColor = c;
}




void SubWindow::setBorderColor( const QColor &c )
{
	m_borderColor = c;
}

void SubWindow::detach()
{
	if (isDetached()) {
		return;
	}

	auto pos = mapToGlobal(widget()->pos());
	widget()->setWindowFlags(Qt::Window);
	widget()->show();
	hide();

	widget()->windowHandle()->setPosition(pos);
}

void SubWindow::attach()
{
	if (! isDetached()) {
		return;
	}
	auto frame = widget()->windowHandle()->frameGeometry();

	widget()->setWindowFlags(Qt::Widget);
	widget()->show();
	show();

	// Delay moving & resizing using event queue. Ensures that this widget is
	// visible first, so that resizing works.
	QObject o; connect(&o, &QObject::destroyed, this, [this, frame]() {
		move(mdiArea()->mapFromGlobal(frame.topLeft()));
		resize(frame.size());
	}, Qt::QueuedConnection);
}


/**
 * @brief SubWindow::moveEvent
 * 
 *  overides the QMdiSubWindow::moveEvent() for saving the position
 *  of the subwindow into m_trackedNormalGeom. This position
 *  will be saved with the project because of an Qt bug wich doesn't
 *  save the right position. look at: https://bugreports.qt.io/browse/QTBUG-256
 * @param event
 */
void SubWindow::moveEvent( QMoveEvent * event )
{
	QMdiSubWindow::moveEvent( event );
	// if the window was moved and ISN'T minimized/maximized/fullscreen,
	// then save the current position
	if( !isMaximized() && !isMinimized() && !isFullScreen() )
	{
		m_trackedNormalGeom.moveTopLeft( event->pos() );
	}
}




/**
 * @brief SubWindow::adjustTitleBar
 * 
 *  Our title bar needs buttons for maximize/restore and close in the right upper corner.
 *  We check if the subwindow is maximizable and put the buttons on the right positions.
 *  At next we calculate the width of the title label and call elideText() for adding
 *  the window title to m_windowTitle (which is a QLabel)
 */
void SubWindow::adjustTitleBar()
{
	// button adjustments
	m_maximizeBtn->hide();
	m_restoreBtn->hide();

	const int rightSpace = 3;
	const int buttonGap = 1;
	const int menuButtonSpace = 24;

	QPoint buttonPos( width() - rightSpace - m_buttonSize.width(), 3 );
	const QPoint buttonStep( m_buttonSize.width() + buttonGap, 0 );

	// the buttonBarWidth depends on the number of buttons.
	// we need it to calculate the width of window title label
	int buttonBarWidth = rightSpace + m_buttonSize.width();

	// set the buttons on their positions.
	// the close button is always needed and on the rightButtonPos
	m_closeBtn->move( buttonPos );
	buttonPos -= buttonStep;

	// here we ask: is the Subwindow maximizable and
	// then we set the buttons and show them if needed
	if( windowFlags() & Qt::WindowMaximizeButtonHint )
	{
		buttonBarWidth = buttonBarWidth + m_buttonSize.width() + buttonGap;
		m_maximizeBtn->move( buttonPos );
		m_restoreBtn->move( buttonPos );
		if ( ! isMaximized() ) {
			m_maximizeBtn->show();
			buttonPos -= buttonStep;
		}
	}

	// we're keeping the restore button around if we open projects
	// from older versions that have saved minimized windows
	if ( isMaximized() || isMinimized() ) {
		m_restoreBtn->show();
		buttonPos -= buttonStep;
	}

	m_detachBtn->move( buttonPos );
	m_detachBtn->show();

	if( widget() )
	{
		// title QLabel adjustments
		m_windowTitle->setAlignment( Qt::AlignHCenter );
		m_windowTitle->setFixedWidth( widget()->width() - ( menuButtonSpace + buttonBarWidth ) );
		m_windowTitle->move( menuButtonSpace,
			( m_titleBarHeight / 2 ) - ( m_windowTitle->sizeHint().height() / 2 ) - 1 );

		// if minimized we can't use widget()->width(). We have to hard code the width,
		// as the width of all minimized windows is the same.
		if( isMinimized() )
		{
			m_windowTitle->setFixedWidth( 120 );
		}

		// truncate the label string if the window is to small. Adds "..."
		elideText( m_windowTitle, widget()->windowTitle() );
		m_windowTitle->setTextInteractionFlags( Qt::NoTextInteraction );
		m_windowTitle->adjustSize();
	}
}



void SubWindow::focusChanged( QMdiSubWindow *subWindow )
{
	if( m_hasFocus && subWindow != this )
	{
		m_hasFocus = false;
		emit focusLost();
	}
	else if( subWindow == this )
	{
		m_hasFocus = true;
	}
}




/**
 * @brief SubWindow::resizeEvent
 * 
 *  At first we give the event to QMdiSubWindow::resizeEvent() which handles
 *  the event on its behavior.
 *
 *  On every resize event we have to adjust our title label.
 * 
 *  At last we store the current size into m_trackedNormalGeom. This size
 *  will be saved with the project because of an Qt bug wich doesn't
 *  save the right size. look at: https://bugreports.qt.io/browse/QTBUG-256
 * 
 * @param event
 */
void SubWindow::resizeEvent( QResizeEvent * event )
{
	// When the parent QMdiArea gets resized, maximized subwindows also gets resized, if any.
	// In that case, we should call QMdiSubWindow::resizeEvent first
	// to ensure we get the correct window state.
	QMdiSubWindow::resizeEvent( event );
	adjustTitleBar();

	// if the window was resized and ISN'T minimized/maximized/fullscreen,
	// then save the current size
	if( !isMaximized() && !isMinimized() && !isFullScreen() )
	{
		m_trackedNormalGeom.setSize( event->size() );
	}
}

bool SubWindow::eventFilter(QObject * obj, QEvent * event)
{
	if (obj != static_cast<QObject *>(widget())) {
		return QMdiSubWindow::eventFilter(obj, event);
	}
	switch (event->type()) {
	case QEvent::WindowStateChange:
		event->accept();
		return true;
	default:
		return QMdiSubWindow::eventFilter(obj, event);
	}
}
