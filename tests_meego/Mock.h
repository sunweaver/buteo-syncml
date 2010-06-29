/*
* This file is part of buteo-syncml package
*
* Copyright (C) 2010 Nokia Corporation. All rights reserved.
*
* Contact: Sateesh Kavuri <sateesh.kavuri@nokia.com>
*
* Redistribution and use in source and binary forms, with or without 
* modification, are permitted provided that the following conditions are met:
*
* Redistributions of source code must retain the above copyright notice, 
* this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice, 
* this list of conditions and the following disclaimer in the documentation 
* and/or other materials provided with the distribution.
* Neither the name of Nokia Corporation nor the names of its contributors may 
* be used to endorse or promote products derived from this software without 
* specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
* THE POSSIBILITY OF SUCH DAMAGE.
* 
*/

#ifndef MOCK_HH
#define MOCK_HH

#include "SyncItem.h"
#include "StoragePlugin.h"
#include "Transport.h"
#include "SyncAgentConfig.h"
#include "SyncTarget.h"
#include "DatabaseHandler.h"
#include "StorageHandler.h"
#include <QDebug>
#include <QFile>
#include "SyncMLMessage.h"

// For test conveniance
using namespace DataSync;

/*! \brief Mock class for testing syncitem dependent code
 */
class MockSyncItem : public SyncItem {
public:
    MockSyncItem ( const SyncItemKey& aKey )
    {
        setKey( aKey );
    }
    virtual ~MockSyncItem()
    {

    }

    virtual qint64 getSize() const
    {
        return iData.size();
    }

    virtual bool read( qint64 aOffset, qint64 aLength, QByteArray& aData ) const
    {
        aData = iData.mid( aOffset, aLength );
        return true;
    }

    virtual bool write( qint64 aOffset, const QByteArray& aData )
    {
        iData.resize( aOffset + aData.size() );
        iData.replace( aOffset, aData.size(), aData );
        return true;
    }

    virtual bool resize( qint64 aLength )
    {
        iData.resize( aLength );
        return true;
    }

protected:
    QByteArray iData;

};

/*! \brief Mock class for testing storageplugin dependent code
 */
class MockStorage: public StoragePlugin {
public:

    MockStorage( const QString& aURI, const QString& aContentFormat = "text/x-vcard",
                 const QString& aContentVersion = "1.0" )
    {

        iSourceURI = aURI;
        iMaxObjSize = 500000;

        ContentFormat format;
        format.iType = aContentFormat;
        format.iVersion = aContentVersion;
        iFormats.append( format );
    }

    virtual ~MockStorage() {}

    virtual const QString& getSourceURI() const
    {
        return iSourceURI;
    }

    virtual qint64 getMaxObjSize() const
    {
        return iMaxObjSize;
    }

    virtual const QList<ContentFormat>& getSupportedFormats() const
    {
        return iFormats;
    }

    virtual const ContentFormat& getPreferredFormat() const
    {
        Q_ASSERT( iFormats.count() > 0 );
        return iFormats[0];
    }

    virtual QByteArray getPluginCTCaps( ProtocolVersion aVersion ) const
    {
        QByteArray ctCaps(
            "<CTCap>"
            "<CTType>text/x-vBookmark</CTType>"
            "<VerCT>1.0</VerCT>"
            "<Property>"
            "<PropName>read</PropName>"
            "<DataType>bool</DataType>"
            "<MaxOccur>1</MaxOccur>"
            "<DisplayName>Read</DisplayName>"
            "</Property>"
            "</CTCap>"
            );

        if ( aVersion == DS_1_2 )
        {
            ctCaps.prepend( "<CTCaps>" );
            ctCaps.append( QByteArray(
                "<CTCap>"
                "<CTType>application/vnd.omads-folder+xml</CTType>"
                "<VerCT>1.0</VerCT>"
                "<Property>"
                "<PropName>read</PropName>"
                "<DataType>bool</DataType>"
                "<MaxOccur>1</MaxOccur>"
                "<DisplayName>Read</DisplayName>"
                "</Property>"
                "</CTCap>"
            ) );
            ctCaps.append( "</CTCaps>" );
        }

        return ctCaps;
    }

    virtual bool getAll( QList<SyncItemKey>& aKeys )
    {
        aKeys << SyncItemKey("1") << SyncItemKey("2") << SyncItemKey("3") << SyncItemKey("5");
        return true;
    }

    virtual bool getModifications( QList<SyncItemKey>& aNewKeys,
                                   QList<SyncItemKey>& aReplacedKeys,
                                   QList<SyncItemKey>& aDeletedKeys,
                                   const QDateTime& /*aTimeStamp*/ )
    {
        aNewKeys << SyncItemKey("1") << SyncItemKey("5") << SyncItemKey("5");
        aReplacedKeys << SyncItemKey("2") << SyncItemKey("3");
        aDeletedKeys << SyncItemKey("1") << SyncItemKey("2") << SyncItemKey("3") << SyncItemKey("5");
        return true;
    }

    virtual SyncItem* newItem()
    {
        return new MockSyncItem( "1" );
    }

    virtual SyncItem* getSyncItem( const SyncItemKey& aKey )
    {


        if (aKey == "") {
            return NULL;
        }
        else {
            return new MockSyncItem( aKey );
        }
    }

    virtual QList<StoragePluginStatus> addItems( const QList<SyncItem*>& aItems )
    {
        QList<StoragePluginStatus> results;

        for( int i = 0; i < aItems.count(); ++i ) {
            results.append( STATUS_OK );
        }

        return results;
    }

    virtual QList<StoragePluginStatus> replaceItems( const QList<SyncItem*>& aItems )
    {
        QList<StoragePluginStatus> results;

        for( int i = 0; i < aItems.count(); ++i ) {
            results.append( STATUS_OK );
        }

        return results;
    }

    virtual QList<StoragePluginStatus> deleteItems( const QList<SyncItemKey>& aKeys )
    {
        QList<StoragePluginStatus> results;

        for( int i = 0; i < aKeys.count(); ++i ) {
            results.append( STATUS_OK );
        }

        return results;
    }

protected:
    QString                 iSourceURI;
    qint64                  iMaxObjSize;
    QList<ContentFormat>    iFormats;

};

class MockTransport : public DataSync::Transport {
    Q_OBJECT
public:
    MockTransport(const QString& file, QObject* parent  = 0) : Transport(parent), iFile(file)
    {}
    virtual ~MockTransport() { };
    virtual void setRemoteLocURI( const QString& )  {};
    virtual qint64 getMaxTxSize() { return 12220; }
    virtual qint64 getMaxRxSize() { return 312312;}
    virtual bool sendSyncML( SyncMLMessage* aMessage) { delete aMessage; aMessage = NULL; return true; }
    virtual bool sendSAN( const QByteArray& /*aMessage*/ ) { return true; }
    virtual bool receive() {
        QFile syncmlFile(iFile);

        if (!syncmlFile.open(QFile::ReadOnly | QFile::Text)) {
            qDebug() << "File Cannot be opened";
        } else  {
            qDebug() << "Handling incoming data.. from " << iFile;
            emit readXMLData(&syncmlFile);
        }
        return true;
    }

private:
    QIODevice* data;
    QString iFile;
};


class MockSyncTarget : public SyncTarget{
public:

    MockSyncTarget(ChangeLog* aChangeLog,
                   StoragePlugin* aStoragePlugin,
                   const SyncMode& aSyncMode,
                   const QString aLocalNextAnchor)
    : SyncTarget(aChangeLog, aStoragePlugin, aSyncMode, aLocalNextAnchor) { }

    ~MockSyncTarget() { }

    bool reverted() {return true;}

private:

};


class MockConfig : public SyncAgentConfig {

public:
    MockConfig() : SyncAgentConfig() {
        addSyncTarget("foo", "bar");
    }

    ~MockConfig() {}

    ProtocolVersion getProtocolVersion() {
        return DS_1_1;
    }

};

#endif
