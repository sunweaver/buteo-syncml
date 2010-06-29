/*
* This file is part of meego-syncml package
*
* Copyright (C) 2010 Nokia Corporation. All rights reserved.
*
* Contact: Sateesh Kavuri <sateesh.kavuri@nokia.com>
*
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
*
* Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
* Neither the name of Nokia Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
* EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
* AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
* THE POSSIBILITY OF SUCH DAMAGE.
* 
*/

#include "CommandHandler.h"
#include "StorageHandler.h"
#include "SyncTarget.h"
#include "LogMacros.h"
#include "SyncAgentConfig.h"
#include "ResponseGenerator.h"
#include "ConflictResolver.h"
#include "DevInfPackage.h"
#include "AlertPackage.h"


using namespace DataSync;

CommandHandler::CommandHandler( const Role& aRole )
 : iRole( aRole )
{
    FUNCTION_CALL_TRACE;
}

CommandHandler::~CommandHandler()
{
    FUNCTION_CALL_TRACE;
}

ResponseStatusCode CommandHandler::handleMap( const MapParams& aMapParams, SyncTarget& aTarget )
{
    FUNCTION_CALL_TRACE;

    for( int i = 0; i < aMapParams.mapItemList.count(); ++i ) {
        addUIDMapping( aTarget, aMapParams.mapItemList[i].source, aMapParams.mapItemList[i].target );
    }

    return SUCCESS;
}


void CommandHandler::handleSync( const SyncParams& aSyncParams,
                                 SyncTarget& aTarget,
                                 StorageHandler& aStorageHandler,
                                 ResponseGenerator& aResponseGenerator,
                                 ConflictResolver& aConflictResolver )
{
    FUNCTION_CALL_TRACE;

    if( !aSyncParams.noResp ) {
        aResponseGenerator.addStatus( aSyncParams, SUCCESS );
    }

    QMap<ItemId, ResponseStatusCode> responses;

    // Batch updates

    for( int i = 0; i < aSyncParams.actionList.count(); ++i ) {

        const SyncActionData& data = aSyncParams.actionList[i];

        QString defaultType = data.meta.type;
        QString defaultFormat = data.meta.format;

        // Process items associated with the command
        for( int a = 0; a < data.items.count(); ++a ) {

            const ItemParams& item = data.items[a];

            // Resolve id of the item
            ItemId id;
            id.iCmdId = data.cmdID;
            id.iItemIndex = a;

            // Resolve type of the item
            QString type;
            if( !item.meta.type.isEmpty() ) {
                type = item.meta.type;
            }
            else {
                type = defaultType;
            }

            // Resolve format of the item
            QString format;
            if( !item.meta.format.isEmpty() ) {
                format = item.meta.format;
            }
            else {
                format = defaultFormat;
            }

            if( data.action == SYNCML_ADD ) {

                // Resolve item key
                QString remoteKey = item.source;

                // Resolve parent
                QString parentKey;

                if( iRole == ROLE_CLIENT ) {
                    // Client might receive SourceParent or TargetParent. SourceParent is used
                    // when server does not yet know the id of parent. TargetParent is used if
                    // server knows the id of the parent.

                    if( !item.sourceParent.isEmpty() ) {
                        parentKey = aTarget.mapToLocalUID( item.sourceParent );
                    }
                    else {
                        parentKey = item.targetParent;
                    }
                }
                else if( iRole == ROLE_SERVER && !item.sourceParent.isEmpty() ) {
                    // Server always receives SourceParent, which must be mapped to local id
                    parentKey = aTarget.mapToLocalUID( item.sourceParent );

                }
                // no else

                LOG_DEBUG( "Processing ADD with item URL:" << remoteKey );

                // Large object chunk
                if( item.moreData ) {

                    // First chunk of large object
                    if( !aStorageHandler.buildingLargeObject() ) {

                        // Size needs to be specified for first chunk
                        if( item.meta.size == 0 ) {
                            LOG_CRITICAL( "No size found for large object:" << id.iCmdId
                                          <<"/" << id.iItemIndex );
                            responses.insert( id, SIZE_REQUIRED );
                        }
                        else if( !aStorageHandler.startLargeObjectAdd( *aTarget.getPlugin(), remoteKey,
                                                                       parentKey, type, format,
                                                                       item.meta.size ) ) {
                            responses.insert( id, COMMAND_FAILED );
                        }

                    }

                    if( aStorageHandler.buildingLargeObject() ) {

                        if( aStorageHandler.appendLargeObjectData( item.Data ) ) {

                            aResponseGenerator.addPackage( new AlertPackage( NEXT_MESSAGE,
                                                                             aTarget.getSourceDatabase(),
                                                                             aTarget.getTargetDatabase() ) );
                            responses.insert( id, CHUNKED_ITEM_ACCEPTED );
                        }
                        else {
                            responses.insert( id, COMMAND_FAILED );
                        }

                    }


                }
                // Last chunk of large object
                else if( aStorageHandler.buildingLargeObject() ) {

                    if( !aStorageHandler.matchesLargeObject( remoteKey ) ) {
                        aResponseGenerator.addPackage( new AlertPackage( NO_END_OF_DATA,
                                                                         aTarget.getSourceDatabase(),
                                                                         aTarget.getTargetDatabase() ) );
                        responses.insert( id, COMMAND_NOT_ALLOWED );
                    }
                    else if( aStorageHandler.appendLargeObjectData( item.Data ) ) {
                        if( !aStorageHandler.finishLargeObject( id ) ) {
                            responses.insert( id, COMMAND_FAILED );
                        }
                    }
                    else {
                        responses.insert( id, COMMAND_FAILED );
                    }

                }
                // Normal object
                else if( !aStorageHandler.addItem( id, *aTarget.getPlugin(), parentKey,
                                                   type, format, item.Data ) ) {
                    responses.insert( id, COMMAND_FAILED );
                }
            }
            else if( data.action == SYNCML_REPLACE ) {

                // Resolve item key
                QString localKey;

                if( iRole == ROLE_CLIENT ) {
                    localKey = item.target;
                }
                else {
                    localKey = aTarget.mapToLocalUID( item.source );
                }

                // Resolve parent
                QString parentKey;

                if( iRole == ROLE_CLIENT ) {
                    // Client might receive SourceParent or TargetParent. SourceParent is used
                    // when server does not yet know the id of parent. TargetParent is used if
                    // server knows the id of the parent.

                    if( !item.sourceParent.isEmpty() ) {
                        parentKey = aTarget.mapToLocalUID( item.sourceParent );
                    }
                    else {
                        parentKey = item.targetParent;
                    }
                }
                else if( iRole == ROLE_SERVER ) {
                    // Server always receives SourceParent, which must be mapped to local id
                    parentKey = aTarget.mapToLocalUID( item.sourceParent );

                }
                // no else

                LOG_DEBUG( "Processing REPLACE with item URL:" << localKey );

                // Large object chunk
                if( item.moreData ) {

                    // First chunk of large object
                    if( !aStorageHandler.buildingLargeObject() ) {

                        // Size needs to be specified for first chunk
                        if( item.meta.size == 0 ) {
                            LOG_CRITICAL( "No size found for large object:" << id.iCmdId
                                           <<"/" << id.iItemIndex );
                            responses.insert( id, SIZE_REQUIRED );
                        }
                        else if( !aStorageHandler.startLargeObjectReplace( *aTarget.getPlugin(), localKey,
                                                                           parentKey, type, format,
                                                                           item.meta.size ) ) {
                            responses.insert( id, COMMAND_FAILED );
                        }

                    }

                    if( aStorageHandler.buildingLargeObject() ) {

                        if( aStorageHandler.appendLargeObjectData( item.Data.toUtf8() ) ) {
                            aResponseGenerator.addPackage( new AlertPackage( NEXT_MESSAGE,
                                                                             aTarget.getSourceDatabase(),
                                                                             aTarget.getTargetDatabase() ) );
                            responses.insert( id, CHUNKED_ITEM_ACCEPTED );
                        }
                        else {
                            responses.insert( id, COMMAND_FAILED );
                        }

                    }


                }
                // Last chunk of large object
                else if( aStorageHandler.buildingLargeObject() ) {

                    if( !aStorageHandler.matchesLargeObject( localKey ) ) {
                        aResponseGenerator.addPackage( new AlertPackage( NO_END_OF_DATA,
                                                                         aTarget.getSourceDatabase(),
                                                                         aTarget.getTargetDatabase() ) );
                        responses.insert( id, COMMAND_NOT_ALLOWED );
                    }
                    else if( aStorageHandler.appendLargeObjectData( item.Data ) ) {
                        if( !aStorageHandler.finishLargeObject( id ) ) {
                            responses.insert( id, COMMAND_FAILED );
                        }
                    }
                    else {
                        responses.insert( id, COMMAND_FAILED );
                    }

                }
                // Normal object
                else if( !aStorageHandler.replaceItem( id, *aTarget.getPlugin(), localKey,
                                                       parentKey, type, format, item.Data ) ) {
                    responses.insert( id, COMMAND_FAILED );
                }


            }
            else if( data.action == SYNCML_DELETE ) {

                // Resolve item key
                QString localKey;

                if( iRole == ROLE_CLIENT ) {
                    localKey = item.target;
                }
                else {
                    localKey = aTarget.mapToLocalUID( item.source );
                }

                LOG_DEBUG( "Processing REPLACE with item URL:" << localKey );

                if( !aStorageHandler.deleteItem( id, localKey ) ) {
                    responses.insert( id, COMMAND_FAILED );
                }
            }
            else {
                responses.insert( id, NOT_SUPPORTED );
            }

        }

    }

    // Commit batches

    QMap<ItemId, CommitResult> results;

    results.unite( aStorageHandler.commitAddedItems( *aTarget.getPlugin() ) );

    ConflictResolver* resolver = NULL;;

    if( resolveConflicts() ) {
        resolver = &aConflictResolver;
    }
    else {
        resolver = NULL;
    }

    results.unite( aStorageHandler.commitReplacedItems( *aTarget.getPlugin(), resolver ) );
    results.unite( aStorageHandler.commitDeletedItems( *aTarget.getPlugin(), resolver ) );

    // Process commit results and convert them to result codes

    for( int i = 0; i < aSyncParams.actionList.count(); ++i ) {

        const SyncActionData& data = aSyncParams.actionList[i];

        // Process items associated with the command
        for( int a = 0; a < data.items.count(); ++a ) {

            const ItemParams& item = data.items[a];
            ItemId id;

            id.iCmdId = data.cmdID;
            id.iItemIndex = a;

            if( !responses.contains( id ) ) {

                if( results.contains( id ) ) {

                    ResponseStatusCode statusCode = COMMAND_FAILED;

                    const CommitResult& result = results.value( id );

                    if( result.iStatus == COMMIT_ADDED ) {

                        statusCode = ITEM_ADDED;
                        addUIDMapping( aTarget, item.source, result.iItemKey );

                    }
                    else if( result.iStatus == COMMIT_REPLACED ) {

                        if( result.iConflict == CONFLICT_LOCAL_WIN ) {

                            if( iRole == ROLE_CLIENT ) {
                                statusCode = RESOLVED_CLIENT_WINNING;
                            }
                            else {
                                statusCode = RESOLVED_WITH_SERVER_DATA;
                            }
                        }
                        else if( result.iConflict == CONFLICT_REMOTE_WIN ) {

                            if( iRole == ROLE_CLIENT ) {
                                statusCode = RESOLVED_WITH_SERVER_DATA;
                            }
                            else {
                                statusCode = RESOLVED_CLIENT_WINNING;
                            }
                        }
                        else {
                            statusCode = SUCCESS;
                        }

                    }
                    else if( result.iStatus == COMMIT_DELETED ) {

                        if( result.iConflict == CONFLICT_LOCAL_WIN ) {

                            if( iRole == ROLE_CLIENT ) {
                                statusCode = RESOLVED_CLIENT_WINNING;
                            }
                            else {
                                statusCode = RESOLVED_WITH_SERVER_DATA;
                            }
                        }
                        else if( result.iConflict == CONFLICT_REMOTE_WIN ) {
                            removeUIDMapping( aTarget, result.iItemKey );

                            if( iRole == ROLE_CLIENT ) {
                                statusCode = RESOLVED_WITH_SERVER_DATA;
                            }
                            else {
                                statusCode = RESOLVED_CLIENT_WINNING;
                            }
                        }
                        else {
                            removeUIDMapping( aTarget, result.iItemKey );
                            statusCode = SUCCESS;
                        }

                    }
                    else if( result.iStatus == COMMIT_DUPLICATE ) {
                        statusCode = ALREADY_EXISTS;
                    }
                    else if( result.iStatus == COMMIT_NOT_DELETED ) {
                        statusCode = ITEM_NOT_DELETED;
                        removeUIDMapping( aTarget, result.iItemKey );
                    }
                    else if( result.iStatus == COMMIT_UNSUPPORTED_FORMAT ) {
                        statusCode = UNSUPPORTED_FORMAT;
                    }
                    else if( result.iStatus == COMMIT_ITEM_TOO_BIG ) {
                        statusCode = REQUEST_SIZE_TOO_BIG;
                    }
                    else if( result.iStatus == COMMIT_NOT_ENOUGH_SPACE ) {
                        statusCode = DEVICE_FULL;
                    }
                    else {
                        statusCode = COMMAND_FAILED;
                    }

                    responses.insert( id, statusCode );

                }
                else {
                    responses.insert( id, COMMAND_FAILED );
                }

            }
        }

    }

    // Process result codes and write corresponding status elements

    for( int i = 0; i < aSyncParams.actionList.count(); ++i ) {

        const SyncActionData& data = aSyncParams.actionList[i];

        // Process items associated with the command
        for( int a = 0; a < data.items.count(); ++a ) {

            const ItemParams& item = data.items[a];

            ItemId id;

            id.iCmdId = data.cmdID;
            id.iItemIndex = a;

            ResponseStatusCode response = responses.value( id );

            if( !data.noResp ) {
                aResponseGenerator.addStatus( data, item, response );
            }

        }

    }

}

void CommandHandler::rejectSync( const SyncParams& aSyncParams, ResponseGenerator& aResponseGenerator,
                                 ResponseStatusCode aResponseCode )
{
    FUNCTION_CALL_TRACE;

    if( !aSyncParams.noResp ) {
        aResponseGenerator.addStatus( aSyncParams, aResponseCode );
    }

    foreach (const SyncActionData& actionData, aSyncParams.actionList) {

        if( !actionData.noResp ) {
            aResponseGenerator.addStatus( actionData, aResponseCode );
        }

    }

}

void CommandHandler::handleStatus(StatusParams* aStatusParams )
{
    FUNCTION_CALL_TRACE;

    ResponseStatusCode statusCode = aStatusParams->data;
    StatusCodeType statusType = getStatusType(statusCode);

    switch (statusType) {
        case INFORMATIONAL:
        {
            // an informational message, no actions needed
            break;
        }

        case SUCCESSFUL:
        {
            // Success, no actions needed
            break;
        }

        case REDIRECTION:
        {
            handleRedirection( statusCode );
            break;
        }

        case ORIGINATOR_EXCEPTION:
        case RECIPIENT_EXCEPTION:
        {
            handleError(statusCode);
            break;
        }

        default:
        {
            // Unknown code
            LOG_DEBUG("Found unknown code: " << statusCode);
            break;
        }
    }

    if (aStatusParams != NULL &&
        ( aStatusParams->cmd == SYNCML_ELEMENT_ADD ||
          aStatusParams->cmd == SYNCML_ELEMENT_REPLACE ||
          aStatusParams->cmd == SYNCML_ELEMENT_DELETE ) ) {
        emit itemAcknowledged( aStatusParams->msgRef, aStatusParams->cmdRef, aStatusParams->sourceRef );
    }
    else if(aStatusParams != NULL && aStatusParams->cmd == SYNCML_ELEMENT_MAP) {
        emit mappingAcknowledged( aStatusParams->msgRef, aStatusParams->cmdRef );
    }

}

void CommandHandler::handleError( ResponseStatusCode aErrorCode )
{
    FUNCTION_CALL_TRACE;

    StatusCodeType statusType = getStatusType( aErrorCode );

    if (statusType == ORIGINATOR_EXCEPTION) {
        switch (aErrorCode) {
            case ALREADY_EXISTS:
            {
                // This merely an informational error that can happen e.g
                // during slow sync. No need to abort the session.
                break;
            }

            default:
            {
                emit abortSync(aErrorCode);
                break;
            }
        }

    }
    else if (statusType == RECIPIENT_EXCEPTION) {

        if (aErrorCode == REFRESH_REQUIRED) {
            /// @todo init resresh sync
        }
        else {
            emit abortSync(aErrorCode);
        }
    }
}

StatusCodeType CommandHandler::getStatusType(ResponseStatusCode aStatus)
{
    FUNCTION_CALL_TRACE;

    const int informationalLowBound        = 100;
    const int informationalHighBound       = 199;
    const int succesfullLowBound           = 200;
    const int successfullHighBound         = 299;
    const int redirectionLowBound          = 300;
    const int redirectionHighBound         = 399;
    const int originatorExceptionLowBound  = 400;
    const int originatorExceptionHighBound = 499;
    const int recipientExceptionLowBound   = 500;
    const int recipientExceptionHighBound  = 599;

    StatusCodeType statusType = UNKNOWN;

    if (aStatus >= informationalLowBound && aStatus < informationalHighBound) {
        statusType = INFORMATIONAL;
    }
    else if (aStatus >= succesfullLowBound && aStatus < successfullHighBound) {
        statusType = SUCCESSFUL;
    }
    else if (aStatus >= redirectionLowBound && aStatus < redirectionHighBound) {
        statusType = REDIRECTION;
    }
    else if (aStatus >= originatorExceptionLowBound && aStatus < originatorExceptionHighBound) {
        statusType = ORIGINATOR_EXCEPTION;
    }
    else if (aStatus >= recipientExceptionLowBound && aStatus < recipientExceptionHighBound) {
        statusType = RECIPIENT_EXCEPTION;
    }
    else {
        statusType = UNKNOWN;
    }

    return statusType;
}

bool CommandHandler::resolveConflicts()
{
    FUNCTION_CALL_TRACE;

    if( iRole == ROLE_CLIENT ) {

        // At the moment, do not try to resolve conflicts on the client side
        return false;

    }
    else {

        // Server should resolve conflicts
        return true;

    }

}

void CommandHandler::addUIDMapping( SyncTarget& aTarget, const QString& aRemoteUID, const SyncItemKey& aLocalUID )
{
    FUNCTION_CALL_TRACE;

    UIDMapping mapping;
    mapping.iRemoteUID = aRemoteUID;
    mapping.iLocalUID = aLocalUID;
    aTarget.addUIDMapping( mapping );

}

void CommandHandler::removeUIDMapping( SyncTarget& aTarget, const SyncItemKey& aLocalUID )
{
    FUNCTION_CALL_TRACE;

    aTarget.removeUIDMapping( aLocalUID );

}

ResponseStatusCode CommandHandler::handleRedirection(ResponseStatusCode /*aRedirectionCode*/)
{
    FUNCTION_CALL_TRACE;

    return NOT_IMPLEMENTED;
}
