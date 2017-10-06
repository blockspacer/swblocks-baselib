/*
 * This file is part of the swblocks-baselib library.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __APPS_BLMESSAGINGECHOSERVER_MESSAGINGECHOSERVERAPP_H_
#define __APPS_BLMESSAGINGECHOSERVER_MESSAGINGECHOSERVERAPP_H_

#include <apps/bl-messaging-echo-server/MessagingEchoServerCmdLine.h>

#include <baselib/messaging/ForwardingBackendProcessingImpl.h>

#include <build/PluginBuildId.h>

#include <baselib/data/models/Http.h>

#include <baselib/http/Globals.h>

#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>

#include <baselib/cmdline/CmdLineAppBase.h>

#include <baselib/crypto/TrustedRoots.h>
#include <baselib/crypto/CryptoBase.h>

#include <baselib/core/FileEncoding.h>
#include <baselib/core/Logging.h>
#include <baselib/core/BaseIncludes.h>

namespace bl
{
    namespace echo
    {
        /**
         * class EchoServerProcessingContext - an echo server message processing context implementation
         */

        template
        <
            typename E = void
        >
        class EchoServerProcessingContextT :
            public messaging::AsyncBlockDispatcher,
            public om::Disposable
        {
            BL_CTR_DEFAULT( EchoServerProcessingContextT, protected )

            BL_DECLARE_OBJECT_IMPL_NO_DESTRUCTOR( EchoServerProcessingContextT )

            BL_QITBL_BEGIN()
                BL_QITBL_ENTRY( messaging::AsyncBlockDispatcher )
                BL_QITBL_ENTRY( om::Disposable )
            BL_QITBL_END( messaging::AsyncBlockDispatcher )

        protected:

            typedef EchoServerProcessingContextT< E >                       this_type;
            typedef om::ObjPtrCopyable< dm::messaging::BrokerProtocol >     broker_protocol_ptr_t;

            const om::ObjPtr< tasks::ExecutionQueue >                       m_eqProcessingQueue;
            const bool                                                      m_isQuietMode;
            const unsigned long                                             m_maxProcessingDelayInMicroseconds;
            const std::string                                               m_tokenType;
            const std::string                                               m_tokenData;
            const om::ObjPtr< data::datablocks_pool_type >                  m_dataBlocksPool;
            const om::ObjPtr< om::Proxy >                                   m_backendReference;

            os::mutex                                                       m_lock;
            cpp::ScalarTypeIniter< bool >                                   m_isDisposed;

            EchoServerProcessingContextT(
                SAA_in      const bool                                      isQuietMode,
                SAA_in      const unsigned long                             maxProcessingDelayInMicroseconds,
                SAA_in      std::string&&                                   tokenType,
                SAA_in      std::string&&                                   tokenData,
                SAA_in      om::ObjPtr< data::datablocks_pool_type >&&      dataBlocksPool,
                SAA_in      om::ObjPtr< om::Proxy >&&                       backendReference
                ) NOEXCEPT
                :
                m_eqProcessingQueue(
                    tasks::ExecutionQueueImpl::createInstance< tasks::ExecutionQueue >(
                        tasks::ExecutionQueue::OptionKeepNone
                        )
                    ),
                m_isQuietMode( isQuietMode ),
                m_maxProcessingDelayInMicroseconds( maxProcessingDelayInMicroseconds ),
                m_tokenType( BL_PARAM_FWD( tokenType ) ),
                m_tokenData( BL_PARAM_FWD( tokenData ) ),
                m_dataBlocksPool( BL_PARAM_FWD( dataBlocksPool ) ),
                m_backendReference( BL_PARAM_FWD( backendReference ) )
            {
            }

            ~EchoServerProcessingContextT() NOEXCEPT
            {
                if( ! m_isDisposed )
                {
                    BL_LOG(
                        Logging::warning(),
                        BL_MSG()
                            << "~EchoServerProcessingContextT() is being called without"
                            << " the object being disposed"
                        );
                }

                disposeInternal();
            }

            void disposeInternal() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                if( m_isDisposed )
                {
                    return;
                }

                m_eqProcessingQueue -> forceFlushNoThrow();
                m_eqProcessingQueue -> dispose();

                m_isDisposed = true;

                BL_NOEXCEPT_END()
            }

            void chkIfDisposed()
            {
                BL_CHK_T(
                    true,
                    m_isDisposed.value(),
                    UnexpectedException(),
                    BL_MSG()
                        << "The server processing context was disposed already"
                    );
            }

            void processingImpl(
                SAA_in      const std::string&                              message,
                SAA_in      const uuid_t&                                   targetPeerId,
                SAA_in      const broker_protocol_ptr_t&                    brokerProtocolIn,
                SAA_in      const om::ObjPtrCopyable< data::DataBlock >&    dataBlock
                )
            {
                using namespace bl::messaging;

                BL_MUTEX_GUARD( m_lock );

                chkIfDisposed();

                if( m_maxProcessingDelayInMicroseconds )
                {
                    const auto randomDelay = random::getUniformRandomUnsignedValue< unsigned long >(
                        m_maxProcessingDelayInMicroseconds
                        );

                    os::sleep( time::microseconds( numbers::safeCoerceTo< long >( randomDelay ) ) );
                }

                if( m_isQuietMode )
                {
                    return;
                }

                std::string messageAsText = resolveMessage(
                    BL_MSG()
                        << "\nContent size:"
                        << dataBlock -> offset1()
                    );

                const auto& passThroughUserData = brokerProtocolIn -> passThroughUserData();

                if( passThroughUserData )
                {
                    const auto payload =
                        dm::DataModelUtils::castTo< dm::http::HttpRequestMetadataPayload >( passThroughUserData );

                     if( payload -> httpRequestMetadata() )
                     {
                        const auto& requestMetadata = payload -> httpRequestMetadata();

                        const auto pos = requestMetadata -> headers().find( http::HttpHeader::g_contentType );

                        if( pos != std::end( requestMetadata -> headers() ) )
                        {
                            const auto& contentType = pos -> second;

                            messageAsText.append(
                                resolveMessage(
                                    BL_MSG()
                                        << "\nContent type:"
                                        << contentType
                                    )
                                );

                            if(
                                contentType == http::HttpHeader::g_contentTypeJsonUtf8 ||
                                contentType == http::HttpHeader::g_contentTypeJsonIso8859_1
                                )
                            {
                                /*
                                 * The content type is JSON, format as pretty JSON
                                 */

                                const auto pair = MessagingUtils::deserializeBlockToObjects( dataBlock );

                                messageAsText.append( "\n\n" );

                                messageAsText.append(
                                    dm::DataModelUtils::getDocAsPrettyJsonString( pair.second /* payload */ )
                                    );
                            }

                            if(
                                contentType == http::HttpHeader::g_contentTypeXml ||
                                contentType == http::HttpHeader::g_contentTypePlainText ||
                                contentType == http::HttpHeader::g_contentTypePlainTextUtf8
                                )
                            {
                                /*
                                 * The content type is printed as text
                                 */

                                messageAsText.append( "\n\n" );

                                messageAsText.append(
                                    dataBlock -> begin(),
                                    dataBlock -> begin() + dataBlock -> offset1()
                                    );
                            }
                        }
                     }
                }

                BL_LOG_MULTILINE(
                    Logging::debug(),
                    BL_MSG()
                        << "\n**********************************************\n\n"
                        << message
                        << "\n\nTarget peer id: "
                        << uuids::uuid2string( targetPeerId )
                        << "\n\nBroker protocol message:\n"
                        << dm::DataModelUtils::getDocAsPrettyJsonString( brokerProtocolIn )
                        << "\n\nPayload message:\n"
                        << messageAsText
                        << "\n\n"
                    );
            }

            auto createProcessingTaskInternal(
                SAA_in      const std::string&                              message,
                SAA_in      const uuid_t&                                   targetPeerId,
                SAA_in      const broker_protocol_ptr_t&                    brokerProtocolIn,
                SAA_in      const om::ObjPtrCopyable< data::DataBlock >&    dataBlock
                )
                -> om::ObjPtr< tasks::SimpleTaskImpl >
            {
                return tasks::SimpleTaskImpl::createInstance(
                    cpp::bind(
                        &this_type::processingImpl,
                        om::ObjPtrCopyable< this_type, om::Disposable >::acquireRef( this ),
                        message,
                        targetPeerId,
                        brokerProtocolIn,
                        dataBlock
                        )
                    );
            }

            auto createServerProcessingAndResponseTask(
                SAA_in                  const uuid_t&                                       targetPeerId,
                SAA_in                  const om::ObjPtrCopyable< data::DataBlock >&        data
                )
                -> om::ObjPtr< tasks::Task >
            {
                using namespace bl::messaging;
                using namespace bl::tasks;

                BL_MUTEX_GUARD( m_lock );

                chkIfDisposed();

                os::mutex_unique_lock guard;

                const auto backend =
                    m_backendReference -> tryAcquireRef< BackendProcessing >( BackendProcessing::iid(), &guard );

                BL_CHK(
                    nullptr,
                    backend,
                    BL_MSG()
                        << "Backend was not connected"
                    );

                const auto pair = MessagingUtils::deserializeBlockToObjects( data, true /* brokerProtocolOnly */ );

                const auto& brokerProtocolIn = pair.first;

                const auto conversationId = uuids::string2uuid( brokerProtocolIn -> conversationId() );

                const auto brokerProtocol = MessagingUtils::createBrokerProtocolMessage(
                    MessageType::AsyncRpcDispatch,
                    conversationId,
                    m_tokenType,
                    m_tokenData
                    );

                /*
                 * Prepare the HTTP response metadata to pass it as pass through user data
                 * in the broker protocol message part
                 */

                auto responseMetadata = dm::http::HttpResponseMetadata::createInstance();

                responseMetadata -> httpStatusCode( http::Parameters::HTTP_SUCCESS_OK );
                responseMetadata -> contentType( http::HttpHeader::g_contentTypeJsonUtf8 );

                responseMetadata -> headersLvalue()[ http::HttpHeader::g_setCookie ] =
                    "responseCookieName=responseCookieValue;";

                const auto responseMetadataPayload = dm::http::HttpResponseMetadataPayload::createInstance();

                responseMetadataPayload -> httpResponseMetadata( std::move( responseMetadata ) );

                brokerProtocol -> passThroughUserData(
                    dm::DataModelUtils::castTo< dm::Payload >( responseMetadataPayload )
                    );

                /*
                 * The response will echo the same data as the request, so we just need to
                 * re-write / update the broker protocol part of the message in the data block
                 */

                data -> setSize( data -> offset1() );

                const auto protocolDataString =
                    dm::DataModelUtils::getDocAsPackedJsonString( brokerProtocol );

                data -> write( protocolDataString.c_str(), protocolDataString.size() );

                auto messageResponseTask = om::ObjPtrCopyable< Task >(
                    backend -> createBackendProcessingTask(
                        BackendProcessing::OperationId::Put,
                        BackendProcessing::CommandId::None,
                        uuids::nil()                                                    /* sessionId */,
                        BlockTransferDefs::chunkIdDefault(),
                        uuids::string2uuid( brokerProtocolIn -> targetPeerId() )        /* sourcePeerId */,
                        uuids::string2uuid( brokerProtocolIn -> sourcePeerId() )        /* targetPeerId */,
                        data
                        )
                    );

                if( m_isQuietMode && 0L == m_maxProcessingDelayInMicroseconds )
                {
                    /*
                     * Special processing is not really required, just echo back the request
                     * message body as a response message
                     */

                    return messageResponseTask.detachAsUnique();
                }

                /*
                 * Processing is required - create a processing task and then
                 * set the message response task as a continuation task
                 */

                auto processingTask = createProcessingTaskInternal(
                    "Echo server processing",
                    targetPeerId,
                    broker_protocol_ptr_t( brokerProtocolIn ),
                    om::ObjPtrCopyable< data::DataBlock >( data )
                    );

                processingTask -> setContinuationCallback(
                    [ = ]( SAA_inout Task* finishedTask ) -> om::ObjPtr< Task >
                    {
                        BL_UNUSED( finishedTask );

                        return om::copy( messageResponseTask );
                    }
                    );

                return om::moveAs< Task >( processingTask );
            }

            void scheduleIncomingRequest(
                SAA_in                  const uuid_t&                                       targetPeerId,
                SAA_in                  const om::ObjPtrCopyable< data::DataBlock >&        data
                )
            {
                BL_MUTEX_GUARD( m_lock );

                chkIfDisposed();

                const auto dataCopy = data::DataBlock::copy( data, m_dataBlocksPool );

                m_eqProcessingQueue -> push_back(
                    tasks::SimpleTaskWithContinuation::createInstance< tasks::Task >(
                        cpp::bind(
                            &this_type::createServerProcessingAndResponseTask,
                            om::ObjPtrCopyable< this_type, om::Disposable >::acquireRef( this ),
                            targetPeerId,
                            om::ObjPtrCopyable< data::DataBlock >( dataCopy )
                            )
                        )
                    );
            }

        public:

            /*
             * om::Disposable
             */

            virtual void dispose() NOEXCEPT OVERRIDE
            {
                BL_NOEXCEPT_BEGIN()

                BL_MUTEX_GUARD( m_lock );

                disposeInternal();

                BL_NOEXCEPT_END()
            }

            /*
             * messaging::AsyncBlockDispatcher
             */

            virtual auto getAllActiveQueuesIds() -> std::unordered_set< uuid_t > OVERRIDE
            {
                return std::unordered_set< uuid_t >();
            }

            virtual auto tryGetMessageBlockCompletionQueue( SAA_in const uuid_t& targetPeerId )
                -> om::ObjPtr< messaging::MessageBlockCompletionQueue > OVERRIDE
            {
                BL_UNUSED( targetPeerId );

                return nullptr;
            }

            virtual auto createDispatchTask(
                SAA_in                  const uuid_t&                                       targetPeerId,
                SAA_in                  const om::ObjPtr< data::DataBlock >&                data
                )
                -> om::ObjPtr< tasks::Task > OVERRIDE
            {
                BL_MUTEX_GUARD( m_lock );

                chkIfDisposed();

                return tasks::SimpleTaskImpl::createInstance< tasks::Task >(
                    cpp::bind(
                        &this_type::scheduleIncomingRequest,
                        om::ObjPtrCopyable< this_type, om::Disposable >::acquireRef( this ),
                        targetPeerId,
                        om::ObjPtrCopyable< data::DataBlock >( data )
                        )
                    );
            }
        };

        typedef om::ObjectImpl< EchoServerProcessingContextT<> > EchoServerProcessingContext;

        /**
         * @brief Messaging echo server application.
         */

        template
        <
            typename E = void
        >
        class MessagingEchoServerAppT
        {
            BL_DECLARE_STATIC( MessagingEchoServerAppT )

        protected:

            template
            <
                typename E2 = void
            >
            class MessagingEchoServerAppImplT :
                public cmdline::CmdLineAppBase< MessagingEchoServerAppImplT< E2 > >
            {
                BL_CTR_DEFAULT( MessagingEchoServerAppImplT, public )
                BL_NO_COPY_OR_MOVE( MessagingEchoServerAppImplT )

            public:

                typedef cmdline::CmdLineAppBase< MessagingEchoServerAppImplT< E2 > >    base_type;

                void parseArgs(
                    SAA_in                      std::size_t                             argc,
                    SAA_in_ecount( argc )       const char* const*                      argv
                    )
                {
                    BL_UNUSED( argc );
                    BL_UNUSED( argv );

                    /*
                     * We need to set base_type::m_isServer here to ensure the CmdLineAppBase
                     * base class configures logging, priorities and other global stuff correctly
                     */

                    base_type::m_isServer = true;
                }

                void appMain(
                    SAA_in                      std::size_t                             argc,
                    SAA_in_ecount( argc )       const char* const*                      argv
                    )
                {
                    using namespace bl::messaging;
                    using namespace bl::tasks;
                    using namespace bl::echo;

                    MessagingEchoServerCmdLine cmdLine;
                    const auto* command = cmdLine.parseCommandLine( argc, argv );

                    BL_ASSERT( command && command == &cmdLine );
                    BL_UNUSED( command );

                    if( cmdLine.m_help.getValue() )
                    {
                        BL_STDIO_TEXT(
                            {
                                cmdLine.helpMessage( std::cout );
                            }
                            );

                        return;
                    }

                    const auto brokerEndpoints = cmdLine.m_brokerEndpoints.getValue();
                    const auto peerId = uuids::string2uuid( cmdLine.m_peerId.getValue() );

                    const auto noOfConnectionRequested = cmdLine.m_connections.getValue();

                    BL_LOG_MULTILINE(
                        Logging::notify(),
                        BL_MSG()
                            << "BASELIB messaging HTTP gateway parameters:\n"
                            << "\nBuild number: "
                            << BL_PLUGINS_BUILD_ID
                            << "\nServer peer id: "
                            << peerId
                            << "\nEndpoints list: "
                            << str::vectorToString( brokerEndpoints )
                            << "\nSecurity token type default: "
                            << cmdLine.m_tokenTypeDefault.getValue( "<empty>" /* defaultValue */ )
                            << "\nSecurity token data default: "
                            << cmdLine.m_tokenDataDefault.getValue( "<empty>" /* defaultValue */ )
                            << "\nNumber of connections: "
                            << noOfConnectionRequested
                            << "\nMax processing time in milliseconds: "
                            << cmdLine.m_maxProcessingDelayInMilliseconds.getValue( 0UL )
                            << "\nQuiet mode: "
                            << cmdLine.m_quietMode.hasValue()
                        );

                    if( cmdLine.m_verifyRootCA.hasValue() )
                    {
                        /*
                         * An additional root CA was provided on command line (to be used / registered)
                         */

                        const fs::path verifyRootCAPath = cmdLine.m_verifyRootCA.getValue();

                        BL_LOG(
                            Logging::debug(),
                            BL_MSG()
                                << "Registering an additional root CA: "
                                << verifyRootCAPath
                            );

                        crypto::registerTrustedRoot(
                            encoding::readTextFile( fs::normalize( verifyRootCAPath ) ) /* certificatePemText */
                            );
                    }

                    const auto controlToken =
                        SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

                    const auto dataBlocksPool = data::datablocks_pool_type::createInstance();

                    const auto minNoOfConnectionsPerEndpoint = 8U;

                    const auto noOfConnections = std::max< std::size_t >(
                        minNoOfConnectionsPerEndpoint * brokerEndpoints.size(),
                        noOfConnectionRequested
                        );

                    BL_LOG_MULTILINE(
                        Logging::notify(),
                        BL_MSG()
                            << "\nBASELIB messaging echo server is starting...\n"
                        );

                    const auto backendReference = om::ProxyImpl::createInstance< om::Proxy >( false /* strongRef*/ );

                    const auto processingContext = om::lockDisposable(
                        EchoServerProcessingContext::createInstance< messaging::AsyncBlockDispatcher >(
                            cmdLine.m_quietMode.hasValue(),
                            cmdLine.m_maxProcessingDelayInMilliseconds.getValue( 0UL ),
                            cmdLine.m_tokenTypeDefault.getValue( "" ),
                            cmdLine.m_tokenDataDefault.getValue( "" ),
                            om::copy( dataBlocksPool ),
                            om::copy( backendReference )
                            )
                        );

                    {
                        const auto forwardingBackend = om::lockDisposable(
                            ForwardingBackendProcessingFactoryDefaultSsl::create(
                                MessagingBrokerDefaultInboundPort       /* defaultInboundPort */,
                                om::copy( controlToken ),
                                peerId,
                                noOfConnections,
                                cpp::copy( brokerEndpoints ),
                                dataBlocksPool,
                                0U                                      /* threadsCount */,
                                0U                                      /* maxConcurrentTasks */,
                                true                                    /* waitAllToConnect */
                                )
                            );

                        {
                            auto proxy = om::ProxyImpl::createInstance< om::Proxy >( true /* strongRef */ );
                            proxy -> connect( processingContext.get() );
                            forwardingBackend -> setHostServices( std::move( proxy ) );
                        }

                        {
                            BL_SCOPE_EXIT(
                                {
                                    backendReference -> disconnect();
                                }
                                );

                            backendReference -> connect( forwardingBackend.get() );

                            scheduleAndExecuteInParallel(
                                [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                                {
                                    eq -> setOptions( ExecutionQueue::OptionKeepNone );

                                    /*
                                     * Schedule a simple timer to request shutdown if the backend gets disconnected
                                     */

                                    const long disconnectedTimerFrequencyInSeconds = 5L;

                                    const auto onTimer = [ disconnectedTimerFrequencyInSeconds ](
                                        SAA_in          const om::ObjPtrCopyable< Task >&                       shutdownWatcher,
                                        SAA_in          const om::ObjPtrCopyable< TaskControlTokenRW >&         controlToken,
                                        SAA_in          const om::ObjPtrCopyable< BackendProcessing >&          backend
                                        )
                                        -> time::time_duration
                                    {
                                        if( ! backend -> isConnected() )
                                        {
                                            controlToken -> requestCancel();
                                            shutdownWatcher -> requestCancel();
                                        }

                                        return time::seconds( disconnectedTimerFrequencyInSeconds );
                                    };

                                    /*
                                     * Just create a CTRL-C shutdown watcher task and wait for the server
                                     * to be shutdown gracefully
                                     */

                                    const auto shutdownWatcher = ShutdownTaskImpl::createInstance< Task >();

                                    SimpleTimer timer(
                                        cpp::bind< time::time_duration >(
                                            onTimer,
                                            om::ObjPtrCopyable< Task >::acquireRef( shutdownWatcher.get() ),
                                            om::ObjPtrCopyable< TaskControlTokenRW >::acquireRef( controlToken.get() ),
                                            om::ObjPtrCopyable< BackendProcessing >::acquireRef( forwardingBackend.get() )
                                            ),
                                        time::seconds( disconnectedTimerFrequencyInSeconds )            /* defaultDuration */,
                                        time::seconds( 0L )                                             /* initDelay */
                                        );

                                    eq -> push_back( shutdownWatcher );
                                    eq -> wait( shutdownWatcher );
                                });
                        }
                    }

                    /*
                     * The messaging echo task should always exit with non-zero exit code to trigger alerts
                     */

                    base_type::m_exitCode = 1;
                }
            };

            typedef MessagingEchoServerAppImplT<> MessagingEchoServerAppImpl;

        public:

            static int main(
                SAA_in                      std::size_t                             argc,
                SAA_in_ecount( argc )       const char* const*                      argv
                )
            {
                Logging::setLevel( Logging::LL_DEBUG, true /* global */ );

                /*
                 * Relax the protocol to TLS 1.0 for allowing the gateway to use
                 * authorization servers that might not yet support TLS 1.1 and TLS 1.2
                 */

                crypto::CryptoBase::isEnableTlsV10( true );

                MessagingEchoServerAppImpl app;

                return app.main( argc, argv );
            }
        };

        typedef MessagingEchoServerAppT<> MessagingEchoServerApp;

    } // echo

} // bl

#endif /* __APPS_BLMESSAGINGECHOSERVER_MESSAGINGECHOSERVERAPP_H_ */