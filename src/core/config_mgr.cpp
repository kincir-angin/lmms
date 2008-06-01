#ifndef SINGLE_SOURCE_COMPILE

/*
 * config_mgr.cpp - implementation of class configManager
 *
 * Copyright (c) 2005-2007 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * 
 * This file is part of Linux MultiMedia Studio - http://lmms.sourceforge.net
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


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <Qt/QtXml>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtGui/QMessageBox>
#include <QtGui/QApplication>


#include "config_mgr.h"
#include "main_window.h"



configManager * configManager::s_instanceOfMe = NULL;


configManager::configManager( void ) :
	m_lmmsRcFile( QDir::home().absolutePath() + "/.lmmsrc.xml" ),
	m_workingDir( QDir::home().absolutePath() + "/lmms" ),
	m_dataDir( qApp->applicationDirPath()
#ifndef BUILD_WIN32
					.section( '/', 0, -2 )
#endif
							+ "/share/lmms/" ),
	m_artworkDir( defaultArtworkDir() ),
	m_pluginDir( qApp->applicationDirPath()
#ifdef BUILD_WIN32
					+ QDir::separator()
#else
				.section( '/', 0, -2 ) + "/lib/lmms/"
#endif
									),
	m_vstDir( QDir::home().absolutePath() ),
	m_flDir( QDir::home().absolutePath() )
{
}




configManager::~configManager()
{
	saveConfigFile();
}




/*void configManager::openWorkingDir( void )
{
	QString new_dir = QFileDialog::getExistingDirectory( this,
					tr( "Choose LMMS working directory" ),
								m_workingDir );
	if( new_dir != QString::null )
	{
		m_wdLineEdit->setText( new_dir );
	}
}*/




void configManager::setWorkingDir( const QString & _wd )
{
	m_workingDir = _wd;
}




void configManager::setVSTDir( const QString & _vd )
{
	m_vstDir = _vd;
}




void configManager::setArtworkDir( const QString & _ad )
{
	m_artworkDir = _ad;
}




void configManager::setFLDir( const QString & _fd )
{
	m_flDir = _fd;
}




void configManager::setLADSPADir( const QString & _fd )
{
	m_ladDir = _fd;
}




void configManager::setSTKDir( const QString & _fd )
{
#ifdef HAVE_STK_H
	m_stkDir = _fd;
#endif
}



/*
void configManager::accept( void )
{
	if( m_workingDir.right( 1 ) != "/" )
	{
		m_workingDir += '/';
	}
	if( !QDir( m_workingDir ).exists() )
	{
		if( QMessageBox::question
				( 0, tr( "Directory not existing" ),
						tr( "The directory you "
							"specified does not "
							"exist. Create it?" ),
					QMessageBox::Yes, QMessageBox::No )
			== QMessageBox::Yes )
		{
			QDir().mkpath( m_workingDir );
		}
		else
		{
			switchPage( m_pageWorkingDir );
			return;
		}
	}

	QDir().mkpath( userProjectsDir() );
	QDir().mkpath( userSamplesDir() );
	QDir().mkpath( userPresetsDir() );

	saveConfigFile();

	QDialog::accept();
}
*/



void configManager::addRecentlyOpenedProject( const QString & _file )
{
	m_recentlyOpenedProjects.removeAll( _file );
	if( m_recentlyOpenedProjects.size() > 15 )
	{
		m_recentlyOpenedProjects.removeLast();
	}
	m_recentlyOpenedProjects.push_front( _file );
}




const QString & configManager::value( const QString & _class,
					const QString & _attribute ) const
{
	if( m_settings.contains( _class ) )
	{
		for( stringPairVector::const_iterator it =
						m_settings[_class].begin();
					it != m_settings[_class].end(); ++it )
		{
			if( ( *it ).first == _attribute )
			{
				return( ( *it ).second );
			}
		}
	}
	static QString empty;
	return( empty );
}




void configManager::setValue( const QString & _class,
				const QString & _attribute,
				const QString & _value )
{
	if( m_settings.contains( _class ) )
	{
		for( stringPairVector::iterator it = m_settings[_class].begin();
					it != m_settings[_class].end(); ++it )
		{
			if( ( *it ).first == _attribute )
			{
				( *it ).second = _value;
				return;
			}
		}
	}
	// not in map yet, so we have to add it...
	m_settings[_class].push_back( qMakePair( _attribute, _value ) );
}




bool configManager::loadConfigFile( void )
{
	// read the XML file and create DOM tree
	QFile cfg_file( m_lmmsRcFile );
	QDomDocument dom_tree;

	if( cfg_file.open( QIODevice::ReadOnly ) )
	{
		if( !dom_tree.setContent( &cfg_file ) )
		{
			return( FALSE );
		}
		cfg_file.close();
	}

	// get the head information from the DOM
	QDomElement root = dom_tree.documentElement();

	QDomNode node = root.firstChild();

	// create the settings-map out of the DOM
	while( !node.isNull() )
	{
		if( node.isElement() && node.toElement().hasAttributes () )
		{
			stringPairVector attr;
			QDomNamedNodeMap node_attr =
						node.toElement().attributes();
			for( int i = 0; i < node_attr.count(); ++i )
			{
				QDomNode n = node_attr.item( i );
				if( n.isAttr() )
				{
					attr.push_back( qMakePair(
							n.toAttr().name(),
							n.toAttr().value() ) );
				}
			}
			m_settings[node.nodeName()] = attr;
		}
		else if( node.nodeName() == "recentfiles" )
		{
			m_recentlyOpenedProjects.clear();
			QDomNode n = node.firstChild();
			while( !n.isNull() )
			{
				if( n.isElement() &&
					n.toElement().hasAttributes() )
				{
					m_recentlyOpenedProjects <<
						n.toElement().
							attribute( "path" );
				}
				n = n.nextSibling();
			}
		}
		node = node.nextSibling();
	}

	if( value( "paths", "artwork" ) != "" )
	{
		m_artworkDir = value( "paths", "artwork" );
		if( QDir( m_artworkDir ).exists() == FALSE )
		{
			m_artworkDir = defaultArtworkDir();
		}
		if( m_artworkDir.right( 1 ) != "/" )
		{
			m_artworkDir += "/";
		}
	}
	m_workingDir = value( "paths", "workingdir" );
	m_vstDir = value( "paths", "vstdir" );
	m_flDir = value( "paths", "fldir" );
	m_ladDir = value( "paths", "laddir" );
#ifdef HAVE_STK_H
	m_stkDir = value( "paths", "stkdir" );
#endif

	if( m_vstDir == "" )
	{
		m_vstDir = QDir::home().absolutePath();
	}

	if( m_flDir == "" )
	{
		m_flDir = QDir::home().absolutePath();
	}

	if( m_ladDir == "" )
	{
		m_ladDir = "/usr/lib/ladspa/:/usr/local/lib/ladspa/";
	}

#ifdef HAVE_STK_H
	if( m_stkDir == "" )
{
	m_stkDir = "/usr/share/stk/rawwaves/";
}
#endif

	QDir::setSearchPaths( "resources", QStringList() << artworkDir()
						<< defaultArtworkDir() );

	return( TRUE );
}




void configManager::saveConfigFile( void )
{
	setValue( "paths", "artwork", m_artworkDir );
	setValue( "paths", "workingdir", m_workingDir );
	setValue( "paths", "vstdir", m_vstDir );
	setValue( "paths", "fldir", m_flDir );
	setValue( "paths", "laddir", m_ladDir );
#ifdef HAVE_STK_H
	setValue( "paths", "stkdir", m_stkDir );
#endif

	QDomDocument doc( "lmms-config-file" );

	QDomElement lmms_config = doc.createElement( "lmms" );
	lmms_config.setAttribute( "version", VERSION );
	doc.appendChild( lmms_config );

	for( settingsMap::iterator it = m_settings.begin();
						it != m_settings.end(); ++it )
	{
		QDomElement n = doc.createElement( it.key() );
		for( stringPairVector::iterator it2 = ( *it ).begin();
						it2 != ( *it ).end(); ++it2 )
		{
			n.setAttribute( ( *it2 ).first, ( *it2 ).second );
		}
		lmms_config.appendChild( n );
	}

	QDomElement recent_files = doc.createElement( "recentfiles" );

	for( QStringList::iterator it = m_recentlyOpenedProjects.begin();
				it != m_recentlyOpenedProjects.end(); ++it )
	{
		QDomElement n = doc.createElement( "file" );
		n.setAttribute( "path", *it );
		recent_files.appendChild( n );
	}
	lmms_config.appendChild( recent_files );

	QString xml = "<?xml version=\"1.0\"?>\n" + doc.toString( 2 );

	QFile outfile( m_lmmsRcFile );
	if( !outfile.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
	{
		QMessageBox::critical( NULL,
			mainWindow::tr( "Could not save config-file" ),
			mainWindow::tr( "Could not save configuration file %1. "
					"You're probably not permitted to "
					"write to this file.\n"
					"Please make sure you have write-"
					"access to the file and try again." ).
							arg( m_lmmsRcFile  ) );
		return;
	}

	outfile.write( xml.toUtf8().constData(), xml.length() );
	outfile.close();
}



#endif
