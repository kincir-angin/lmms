/*
 * remote_plugin.h - base class providing RPC like mechanisms
 *
 * Copyright (c) 2008 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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


#ifndef _REMOTE_PLUGIN_H
#define _REMOTE_PLUGIN_H

#include "lmmsconfig.h"
#include "mixer.h"
#include "midi.h"

#include <vector>
#include <string>
#include <cassert>

#ifdef LMMS_BUILD_WIN32

#if QT_VERSION >= 0x040400
#include <QtCore/QSharedMemory>
#else
#error win32-build requires at least Qt 4.4.0
#endif

#else

#define USE_NATIVE_SHMEM

#ifdef LMMS_HAVE_SYS_IPC_H
#include <sys/ipc.h>
#endif

#ifdef LMMS_HAVE_SYS_SHM_H
#include <sys/shm.h>
#endif

#endif


#ifdef LMMS_HAVE_SEMAPHORE_H
#include <semaphore.h>
#endif


#ifdef BUILD_REMOTE_PLUGIN_CLIENT
#define COMPILE_REMOTE_PLUGIN_BASE
#else
#include <QtCore/QProcess>
#endif


// 4000 should be enough - this way we only need to allocate one page
const int SHM_FIFO_SIZE = 4000;

// implements a FIFO inside a shared memory segment
class shmFifo
{
	// need this union to handle different sizes of sem_t on 32 bit
	// and 64 bit platforms
	union sem32_t
	{
		sem_t sem;
		char fill[32];
	} ;
	struct shmData
	{
		sem32_t dataSem;	// semaphore for locking this
					// FIFO management data
		sem32_t messageSem;	// semaphore for incoming messages
		volatile int32_t startPtr; // current start of FIFO in memory
		volatile int32_t endPtr;   // current end of FIFO in memory
		char data[SHM_FIFO_SIZE];  // actual data
	} ;

public:
	// constructor for master-side
	shmFifo() :
		m_master( true ),
		m_shmKey( 0 ),
#ifdef USE_NATIVE_SHMEM
		m_shmID( -1 ),
#else
		m_shmObj(),
#endif
		m_data( NULL ),
		m_dataSem( NULL ),
		m_messageSem( NULL ),
		m_lockDepth( 0 )
	{
#ifdef USE_NATIVE_SHMEM
		while( ( m_shmID = shmget( ++m_shmKey, sizeof( shmData ),
					IPC_CREAT | IPC_EXCL | 0600 ) ) == -1 )
		{
		}
		m_data = (shmData *) shmat( m_shmID, 0, 0 );
#else
		do
		{
			m_shmObj.setKey( QString( "%1" ).arg( ++m_shmKey ) );
			m_shmObj.create( sizeof( shmData ) );
		} while( m_shmObj.error() != QSharedMemory::NoError );

		m_data = (shmData *) m_shmObj.data();
#endif
		assert( m_data != NULL );
		m_dataSem = &m_data->dataSem.sem;
		m_messageSem = &m_data->messageSem.sem;
		m_data->startPtr = m_data->endPtr = 0;

		if( sem_init( m_dataSem, 1, 1 ) )
		{
			printf( "could not initialize m_dataSem\n" );
		}
		if( sem_init( m_messageSem, 1, 0 ) )
		{
			printf( "could not initialize m_messageSem\n" );
		}

	}

	// constructor for remote-/client-side - use _shm_key for making up
	// the connection to master
	shmFifo( key_t _shm_key ) :
		m_master( false ),
		m_shmKey( 0 ),
#ifdef USE_NATIVE_SHMEM
		m_shmID( shmget( _shm_key, 0, 0 ) ),
#else
		m_shmObj( QString::number( _shm_key ) ),
#endif
		m_data( NULL ),
		m_dataSem( NULL ),
		m_messageSem( NULL ),
		m_lockDepth( 0 )
	{
#ifdef USE_NATIVE_SHMEM
		if( m_shmID != -1 )
		{
			m_data = (shmData *) shmat( m_shmID, 0, 0 );
		}
#else
		if( m_shmObj.attach() )
		{
			m_data = (shmData *) m_shmObj.data();
		}
#endif
		assert( m_data != NULL );
		m_dataSem = &m_data->dataSem.sem;
		m_messageSem = &m_data->messageSem.sem;
	}

	~shmFifo()
	{
#ifdef USE_NATIVE_SHMEM
		shmdt( m_data );
#endif
		// master?
		if( m_master )
		{
#ifdef USE_NATIVE_SHMEM
			shmctl( m_shmID, IPC_RMID, NULL );
#endif
			sem_destroy( m_dataSem );
			sem_destroy( m_messageSem );
		}
	}

	// do we act as master (i.e. not as remote-process?)
	inline bool isMaster( void ) const
	{
		return( m_master );
	}

	// recursive lock
	inline void lock( void )
	{
		if( ++m_lockDepth == 1 )
		{
			sem_wait( m_dataSem );
		}
	}

	// recursive unlock
	inline void unlock( void )
	{
		if( m_lockDepth > 0 )
		{
			if( --m_lockDepth == 0 )
			{
				sem_post( m_dataSem );
			}
		}
	}

	// wait until message-semaphore is available
	inline void waitForMessage( void )
	{
		sem_wait( m_messageSem );
	}

	// increase message-semaphore
	inline void messageSent( void )
	{
		sem_post( m_messageSem );
	}


	inline int32_t readInt( void )
	{
		int32_t i;
		read( &i, sizeof( i ) );
		return( i );
	}

	inline void writeInt( const int32_t & _i )
	{
		write( &_i, sizeof( _i ) );
	}

	inline std::string readString( void )
	{
		const int len = readInt();
		if( len )
		{
			char * sc = new char[len + 1];
			read( sc, len );
			sc[len] = 0;
			std::string s( sc );
			delete[] sc;
			return( s );
		}
		return std::string();
	}


	inline void writeString( const std::string & _s )
	{
		const int len = _s.size();
		writeInt( len );
		write( _s.c_str(), len );
	}


	inline bool messagesLeft( void )
	{
		int v;
		sem_getvalue( m_messageSem, &v );
		return( v > 0 );
	}


	inline int shmKey( void ) const
	{
		return( m_shmKey );
	}


private:
	static inline int fastMemCpy( void * _dest, const void * _src,
							const int _len )
	{
		// calling memcpy() for just an integer is obsolete overhead
		if( _len == 4 )
		{
			*( (int32_t *) _dest ) = *( (int32_t *) _src );
		}
		else
		{
			memcpy( _dest, _src, _len );
		}
	}

	void read( void * _buf, int _len )
	{
		lock();
		while( _len > m_data->endPtr - m_data->startPtr )
		{
			unlock();
#ifndef LMMS_BUILD_WIN32
			usleep( 5 );
#endif
			lock();
		}
		fastMemCpy( _buf, m_data->data + m_data->startPtr, _len );
		m_data->startPtr += _len;
		// nothing left?
		if( m_data->startPtr == m_data->endPtr )
		{
			// then reset to 0
			m_data->startPtr = m_data->endPtr = 0;
		}
		unlock();
	}

	void write( const void * _buf, int _len )
	{
		lock();
		while( _len > SHM_FIFO_SIZE - m_data->endPtr )
		{
			// if no space is left, try to move data to front
			if( m_data->startPtr > 0 )
			{
				memmove( m_data->data,
					m_data->data + m_data->startPtr,
					m_data->endPtr - m_data->startPtr );
				m_data->endPtr = m_data->endPtr -
							m_data->startPtr;
				m_data->startPtr = 0;
			}
			unlock();
#ifndef LMMS_BUILD_WIN32
			usleep( 5 );
#endif
			lock();
		}
		fastMemCpy( m_data->data + m_data->endPtr, _buf, _len );
		m_data->endPtr += _len;
		unlock();
	}

	bool m_master;
#ifdef USE_NATIVE_SHMEM
	key_t m_shmKey;
	int m_shmID;
#else
	int m_shmKey;
	QSharedMemory m_shmObj;
#endif
	size_t m_shmSize;
	shmData * m_data;
	sem_t * m_dataSem;
	sem_t * m_messageSem;
	int m_lockDepth;

} ;



enum RemoteMessageIDs
{
	IdUndefined,
	IdGeneralFailure,
	IdInitDone,
	IdClosePlugin,
	IdSampleRateInformation,
	IdBufferSizeInformation,
	IdMidiEvent,
	IdStartProcessing,
	IdProcessingDone,
	IdChangeSharedMemoryKey,
	IdChangeInputCount,
	IdChangeOutputCount,
	IdShowUI,
	IdHideUI,
	IdSaveSettingsToString,
	IdSaveSettingsToFile,
	IdLoadSettingsFromString,
	IdLoadSettingsFromFile,
	IdLoadPresetFromFile,
	IdUserBase = 64
} ;



class remotePluginBase
{
public:
	struct message
	{
		message() :
			id( IdUndefined ),
			data()
		{
		}

		message( const message & _m ) :
			id( _m.id ),
			data( _m.data )
		{
		}

		message( int _id ) :
			id( _id ),
			data()
		{
		}

		void addInt( int _i )
		{
			char buf[128];
			buf[0] = 0;
			sprintf( buf, "%d", _i );
			data.push_back( std::string( buf ) );
		}

		int getInt( int _p ) const
		{
			return( atoi( data[_p].c_str() ) );
		}

		bool operator==( const message & _m )
		{
			return( id == _m.id );
		}

		int id;
		std::vector<std::string> data;
	} ;

	remotePluginBase( shmFifo * _in, shmFifo * _out );
	virtual ~remotePluginBase();

	void sendMessage( const message & _m );
	message receiveMessage( void );

	inline bool messagesLeft( void )
	{
		return( m_in->messagesLeft() );
	}


	message waitForMessage( const message & _m,
						bool _busy_waiting = FALSE );

	inline message fetchAndProcessNextMessage( void )
	{
		message m = receiveMessage();
		processMessage( m );
		return m;
	}

	inline bool fetchAndProcessAllMessages( void )
	{
		while( messagesLeft() )
		{
			fetchAndProcessNextMessage();
		}
	}

	virtual bool processMessage( const message & _m ) = 0;


protected:
	const shmFifo * in( void ) const
	{
		return( m_in );
	}

	const shmFifo * out( void ) const
	{
		return( m_out );
	}


private:
	shmFifo * m_in;
	shmFifo * m_out;

} ;



#ifndef BUILD_REMOTE_PLUGIN_CLIENT

class remotePlugin : public remotePluginBase
{
public:
	remotePlugin( const QString & _plugin_executable );
	virtual ~remotePlugin();

	virtual bool processMessage( const message & _m );

	bool process( const sampleFrame * _in_buf,
					sampleFrame * _out_buf, bool _wait );
	bool waitForProcessingFinished( sampleFrame * _out_buf );

	void processMidiEvent( const midiEvent &, const f_cnt_t _offset );

	void updateSampleRate( sample_rate_t _sr )
	{
		message m( IdSampleRateInformation );
		m.addInt( _sr );
		sendMessage( m );
	}

	void showUI( void )
	{
		sendMessage( IdShowUI );
	}

	void hideUI( void )
	{
		sendMessage( IdHideUI );
	}


protected:
	inline void lock( void )
	{
		m_commMutex.lock();
	}

	inline void unlock( void )
	{
		m_commMutex.unlock();
	}


private:
	void resizeSharedProcessingMemory( void );


	bool m_initialized;
	bool m_failed;

	QProcess m_process;

	QMutex m_commMutex;
#ifdef USE_NATIVE_SHMEM
	int m_shmID;
#else
	QSharedMemory m_shmObj;
#endif
	size_t m_shmSize;
	float * m_shm;

	int m_inputCount;
	int m_outputCount;

} ;

#endif


#ifdef BUILD_REMOTE_PLUGIN_CLIENT

class remotePluginClient : public remotePluginBase
{
public:
	remotePluginClient( key_t _shm_in, key_t _shm_out );
	virtual ~remotePluginClient();

	virtual bool processMessage( const message & _m );

	virtual bool process( const sampleFrame * _in_buf,
					sampleFrame * _out_buf ) = 0;

	virtual void processMidiEvent( const midiEvent &,
						const f_cnt_t /* _offset */ )
	{
	}

	inline float * sharedMemory( void )
	{
		return( m_shm );
	}

	virtual void updateSampleRate( sample_rate_t )
	{
	}

	virtual void updateBufferSize( fpp_t )
	{
	}

	inline fpp_t bufferSize( void ) const
	{
		return( m_bufferSize );
	}

	void setInputCount( int _i )
	{
		m_inputCount = _i;
		message m( IdChangeInputCount );
		m.addInt( _i );
		sendMessage( m );
	}

	void setOutputCount( int _i )
	{
		m_outputCount = _i;
		message m( IdChangeOutputCount );
		m.addInt( _i );
		sendMessage( m );
	}


private:
	void setShmKey( key_t _key, int _size );
	void doProcessing( void );

#ifndef USE_NATIVE_SHMEM
	QSharedMemory m_shmObj;
#endif
	float * m_shm;

	int m_inputCount;
	int m_outputCount;

	fpp_t m_bufferSize;

} ;

#endif





#ifdef COMPILE_REMOTE_PLUGIN_BASE

#ifndef BUILD_REMOTE_PLUGIN_CLIENT
#include <QtCore/QCoreApplication>
#endif


remotePluginBase::remotePluginBase( shmFifo * _in, shmFifo * _out ) :
	m_in( _in ),
	m_out( _out )
{
}




remotePluginBase::~remotePluginBase()
{
	delete m_in;
	delete m_out;
}




void remotePluginBase::sendMessage( const message & _m )
{
	m_out->lock();
	m_out->writeInt( _m.id );
	m_out->writeInt( _m.data.size() );
	for( int i = 0; i < _m.data.size(); ++i )
	{
		m_out->writeString( _m.data[i] );
	}
	m_out->unlock();
	m_out->messageSent();
}




remotePluginBase::message remotePluginBase::receiveMessage( void )
{
	m_in->waitForMessage();
	m_in->lock();
	message m;
	m.id = m_in->readInt();
	const int s = m_in->readInt();
	for( int i = 0; i < s; ++i )
	{
		m.data.push_back( m_in->readString() );
	}
	m_in->unlock();
	return( m );
}




remotePluginBase::message remotePluginBase::waitForMessage(
							const message & _wm,
							bool _busy_waiting )
{
	while( 1 )
	{
#ifndef BUILD_REMOTE_PLUGIN_CLIENT
		if( _busy_waiting && !messagesLeft() )
		{
			QCoreApplication::processEvents(
						QEventLoop::AllEvents, 50 );
			continue;
		}
#endif
		message m = receiveMessage();
		processMessage( m );
		if( m.id == _wm.id )
		{
			return( m );
		}
		else if( m.id == IdGeneralFailure )
		{
			return( m );
		}
	}
}


#endif





#ifdef BUILD_REMOTE_PLUGIN_CLIENT


remotePluginClient::remotePluginClient( key_t _shm_in, key_t _shm_out ) :
	remotePluginBase( new shmFifo( _shm_in ),
				new shmFifo( _shm_out ) ),
#ifndef USE_NATIVE_SHMEM
	m_shmObj(),
#endif
	m_shm( NULL ),
	m_inputCount( DEFAULT_CHANNELS ),
	m_outputCount( DEFAULT_CHANNELS ),
	m_bufferSize( DEFAULT_BUFFER_SIZE )
{
	sendMessage( IdSampleRateInformation );
	sendMessage( IdBufferSizeInformation );
}




remotePluginClient::~remotePluginClient()
{
	sendMessage( IdClosePlugin );

#ifdef USE_NATIVE_SHMEM
	shmdt( m_shm );
#endif
}




bool remotePluginClient::processMessage( const message & _m )
{
	message reply_message( _m.id );
	bool reply = false;
	switch( _m.id )
	{
		case IdGeneralFailure:
			return( false );

		case IdSampleRateInformation:
			updateSampleRate( _m.getInt( 0 ) );
			break;

		case IdBufferSizeInformation:
			m_bufferSize = _m.getInt( 0 );
			updateBufferSize( m_bufferSize );
			break;

		case IdClosePlugin:
			return( false );

		case IdMidiEvent:
			processMidiEvent(
				midiEvent( static_cast<MidiEventTypes>(
							_m.getInt( 0 ) ),
						_m.getInt( 1 ),
						_m.getInt( 2 ),
						_m.getInt( 3 ) ),
							_m.getInt( 4 ) );
			break;

		case IdStartProcessing:
			doProcessing();
			reply_message.id = IdProcessingDone;
			reply = true;
			break;

		case IdChangeSharedMemoryKey:
			setShmKey( _m.getInt( 0 ), _m.getInt( 1 ) );
			break;

		case IdUndefined:
		default:
			break;
	}
	if( reply )
	{
		sendMessage( reply_message );
	}

	return( true );
}




void remotePluginClient::setShmKey( key_t _key, int _size )
{
#ifdef USE_NATIVE_SHMEM
	if( m_shm != NULL )
	{
		shmdt( m_shm );
		m_shm = NULL;
	}

	// only called for detaching SHM?
	if( _key == 0 )
	{
		return;
	}

	int shm_id = shmget( _key, _size, 0 );
	if( shm_id == -1 )
	{
		fprintf( stderr, "failed getting shared memory\n" );
	}
	else
	{
		m_shm = (float *) shmat( shm_id, 0, 0 );
	}
#else
	m_shmObj.setKey( QString::number( _key ) );
	if( m_shmObj.attach() )
	{
		m_shm = (float *) m_shmObj.data();
	}
	else
	{
		fprintf( stderr, "failed getting shared memory\n" );
	}
#endif
}




void remotePluginClient::doProcessing( void )
{
	if( m_shm != NULL )
	{
		process( (sampleFrame *)( m_inputCount > 0 ? m_shm : NULL ),
				(sampleFrame *)( m_shm +
					( m_inputCount*m_bufferSize ) ) );
	}
}



#endif

#endif
