#pragma once
//-----------------------------------------------------------------------------
// Class:	NPLMiniRuntime
// Authors:	LiXizhi
// Company: ParaEngine Co.
// Date:	2010.4.8
// Desc: NPLMiniRuntime is used by any standalone exe/dll to act as a NPL runtime 
// without the need to link with the full-featured ParaEngine/NPL library. 
// The mini runtime provides basic implementation to send/receive NPL pure data messages. 
// Therefore, it is easy to build loosely coupled systems using NPL for messaging with minimum code overhead and dependency. 
// To distinguish it from the real NPL runtime, we have put it in the NPLInterface namespace
// 
// See comment of CNPLMiniRuntimeT for example usage
//-----------------------------------------------------------------------------

/** @def if USE_BOOST_SIGNAL_FILE_HANDLER is defined, we will allow multiple file handlers to subscribe to the same filename target. 
* but the caller will need to link with the boost signal lib, which usually increase about 30KB in exe size. if not defined, only one handler is allowed per file. 
* The latest registered handler will overwrite the previous one. 
* NOTE: Do not modify this file, define it outsize this file if you like. 
*/
// #define USE_BOOST_SIGNAL_FILE_HANDLER

#include <boost/intrusive_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include "PEtypes.h"
#include "NPLTypes.h"
#include "INPL.h"
#include "INPLRuntime.h"
#include "NPLInterface.hpp"

#include "util/Mutex.hpp"
#include "util/Semaphore.hpp"

#include <vector>
#include <map>
#include <set>
#include <queue>

#ifdef USE_BOOST_SIGNAL_FILE_HANDLER
#include <boost/signals2.hpp>
#else
#include <boost/function.hpp>
#endif

namespace NPLInterface
{
#pragma region Headers
	/** NPL mini message is used by mini runtime internally */
	struct NPLMiniMessage
	{
		NPLMiniMessage(){};
		NPLMiniMessage(const char * sNPLFilename, const char* sCode, int nCodeLength=0)
			: m_type(0)
		{
			if(sNPLFilename)
				m_sFilename = sNPLFilename;
			if(sCode)
			{
				if(nCodeLength <= 0)
					m_sCode = sCode;
				else
				{
					m_sCode.reserve(nCodeLength);
					m_sCode = sCode;
				}
			}
		}
		/** the target filename. */
		std::string m_sFilename;

		/** the pure data code. We can serialize it to/from NPLObjectProxy easily. */
		std::string m_sCode;

		/** message type*/
		int m_type;
	};

	/**
	* a globally unique name of a NPL file name instance. 
	* The string format of an NPL file name is like below. 
	* [(sRuntimeStateName|gl)][sNID:]sRelativePath[@sDNSServerName]
	*/
	template <class StringType = std::string>
	struct NPLFileNameT
	{
		/** the runtime state name that the file belongs to. It usually specifies which thread the file instance is running in. 
		If empty or "gl", it means the current or default runtime state. 
		It must be a string with only alphabetic letters. 
		*/
		StringType sRuntimeStateName;
		/** the NPL runtime server ID that the file belongs to. It usually represents a network endpoint (IP/port). 
		* However, instead of using IP address "192.168.0.111/60001", we usually use email like addresses, like "1001@paraengine.com"
		* the email address will latter be resolved to IP/port. if empty, it means the local runtime environment.
		* Specially, "all@local" means all remote NIDs connecting to this local machine.
		*/
		StringType sNID;
		/** the relative file path of the NPL file in the Runtime's file system. it uses forward slashes such as "script/sample.lua" 
		* if empty, it defaults to "script/empty.lua"
		*/
		StringType sRelativePath;
		/** the DNS file name. this is a global server where the sNID can be authenticated and converted to IP/port.  
		* if empty, it first defaults to the server part of sNID, if that is empty too, default to the current one in the local runtime environment. 
		* we rarely need to explicitly specify DNS server.
		*/ 
		StringType sDNSServerName;

	public:
		NPLFileNameT(){}

		/**
		* create the NPL file name object from a string. 
		* The string is of the following file format
		* @param filename: [(sRuntimeStateName|gl)][sNID:]sRelativePath[@sDNSServerName]
		* the following is a list of all valid file name combinations: 
		*	"user001@paraengine.com:script/hello.lua"	-- a file of user001 in its default gaming thread
		*	"(world1)server001@paraengine.com:script/hello.lua"		-- a file of server001 in its thread world1
		*	"(worker1)script/hello.lua"			-- a local file in the thread worker1
		*	"(gl)script/hello.lua"			-- a glia (local) file in the current runtime state's thread
		*	"script/hello.lua"			-- a file in the current thread. For a single threaded application, this is usually enough.
		*/
		NPLFileNameT(const char * filename){
			FromString(filename);
		}

		/** set the relaive_Path 
		* @param sPath: where the path string is.
		* @param nCount: the number of characters to copy from sPath. If this is negative. the whole sPath will be read.
		*/
		void SetRelativePath(const char* sPath, int nCount=-1){
			if(nCount <= 0)
			{
				sRelativePath = sPath;
				nCount = (int)sRelativePath.size();
			}
			else
			{
				sRelativePath.assign(sPath, nCount);
			}
			for(int i=0; i<nCount ;i++)
			{
				if(sRelativePath[i] == '\\')
					sRelativePath[i] = '/';
			}
		}

	public:
		/** convert to string of the following format
		* [(sRuntimeStateName|gl)][sNID:]sRelativePath[@sDNSServerName]
		*/
		std::string ToString(){
			string output;
			ToString(output);
			return output;
		}
		void ToString(std::string & output){
			output.clear();
			if(!sRuntimeStateName.empty())
			{
				output.append("(");
				output.append(sRuntimeStateName);
				output.append(")");
			}

			// the activation type
			if( ! sNID.empty() )
				output.append(sNID);
			if( ! sRelativePath.empty() )
				output.append(sRelativePath);
			if( ! sDNSServerName.empty() )
			{
				output.append("@");
				output.append(sDNSServerName);
			}
		}

		/** reset from string
		* @see NPLFileName() for details.
		*/
		void FromString(const char* sFilePath){
			// for empty string, default to local Glia file.
			if(sFilePath[0] == '\0')
			{
				// FromString("(gl)script/empty.lua");
				sRuntimeStateName.clear();
				sNID.clear();
				sDNSServerName.clear();
				sRelativePath.clear();
				return;
			}

			int i = 0;
			int nNIDIndex = 0;
			int nRelativePathIndex;
			int nDNSServerIndex;

			bool bExplicitActivationType = false;
			/// get the activation type
			if(sFilePath[i] == '(')
			{
				bExplicitActivationType = true;
				i++;
				while( (sFilePath[i]!=')') && (sFilePath[i]!='\0'))
				{
					i++;
				}
				i++;
				if( !(i==4 && (sFilePath[1]=='g') && (sFilePath[2]=='l')) )
					sRuntimeStateName.assign(sFilePath+1, i-2);
				else
					sRuntimeStateName.clear();
				nNIDIndex = i;
			}

			/// get namespace
			while( (sFilePath[i]!=':') && (sFilePath[i]!='\0'))
				i++;

			if(sFilePath[i]=='\0')
			{
				sNID.clear();
				sRelativePath.assign(sFilePath+nNIDIndex, i-nNIDIndex);
				sDNSServerName.clear();
				return;
			}
			else 
			{
				if(i>nNIDIndex)
					sNID.assign(sFilePath+nNIDIndex, i-nNIDIndex);
				else
					sNID.clear();

				/// get relative path
				nRelativePathIndex = i+1;
				i++;
			}

			/// get relative path
			while( (sFilePath[i]!='@') && (sFilePath[i]!='\0') )
				i++;
			SetRelativePath(sFilePath+nRelativePathIndex, i - nRelativePathIndex);

			if(sFilePath[i]=='\0')
			{
				sDNSServerName.clear();
				return;
			}
			else 
				i++;

			/// get DNS server name
			nDNSServerIndex = i;

			while( sFilePath[i]!='\0')
				i++;
			sDNSServerName.assign(sFilePath+nDNSServerIndex, i - nDNSServerIndex);
		}
	};
	typedef NPLFileNameT<> NPLFileName;

#pragma endregion Headers

#pragma region CNPLMiniState
	/**
	* One can usually implement one or more methods to make it a standalone runtime state. 
	* Depending on how you implement the activate() method, the message can be handled either in the calling thread or in the main thread. 
	* the main thread is the thread where NPLRuntime::Run() method is called at regular interval to process the message. 
	*
	* the default implementation can register message handler according to filename using callbacks.
	* please note that filename is usually used as message target in traditional message system. 
	* Since we use std::string as filename internally, hence if the file name is less than 16 bytes, no memory allocation is needed. 
	* Note: this only applies to MiniState, for full featured NPL runtime, there is no such limit. 
	*/
	class CNPLMiniState : public NPL::INPLRuntimeState,
		public boost::enable_shared_from_this<CNPLMiniState>,
		private boost::noncopyable
	{
	public:
#ifdef USE_BOOST_SIGNAL_FILE_HANDLER
		typedef boost::signals2::signal< void(int, void*) >  NPLFileActivateHandler_t;
		typedef NPLFileActivateHandler_t::slot_type NPLFileActivateHandlerCallback_t;
		//typedef boost::function< void(int, void*) > NPLFileActivateHandlerCallback_t;
		typedef std::map<std::string, boost::shared_ptr<typename NPLFileActivateHandler_t> > NPLFileHandlerMap_t;
#else
		typedef boost::function< void(int, void*) >  NPLFileActivateHandler_t;
		typedef boost::function< void(int, void*) > NPLFileActivateHandlerCallback_t;
		typedef std::map<std::string, typename NPLFileActivateHandler_t> NPLFileHandlerMap_t;
#endif
		typedef std::queue<NPLMiniMessage> NPLMiniMessageQueue_t;
		

		CNPLMiniState(const char* name=NULL): m_current_msg_length(0), m_current_msg(NULL),m_pNPLRuntime(NULL), m_processed_msg_count(0) {
			if(name != NULL){
				m_name = name;
			}
		};
		virtual ~CNPLMiniState() {};

		void Init(){}

		/** construct this to ensure matching calls to SetCurrentMessage(). */
		class CCurrentMessage
		{
		public:
			CCurrentMessage(CNPLMiniState* pState, const char* msg, int nLength):m_pState(pState){
				if(m_pState)
					m_pState->SetCurrentMessage(msg,nLength);
			}
			~CCurrentMessage(){
				if(m_pState)
					m_pState->SetCurrentMessage(NULL, 0);
			}
			CNPLMiniState* m_pState;
		};

	public:
		/** return the name of this runtime state. if "", it is considered an anonymous name */
		virtual const std::string& GetName() const {return m_name;};

		/**
		* activate the specified file. It can either be local or remote file. 
		*/
		virtual int activate(const char * sNPLFilename, const char* sCode, int nCodeLength=0, int priority=2, int reliability=4){
			// Note: if one wants to process in the calling thread, just inherit and do following. 
			// NPLInterface::NPLObjectProxy tabMsg = NPLInterface::NPLHelper::MsgStringToNPLTable(sCode, nCodeLength);
			// OnMessageCallback(tabMsg);

			ParaEngine::Mutex::ScopedLock lock_(m_mutex);
			m_input_queue.push(NPLMiniMessage(sNPLFilename, sCode, nCodeLength));
			return NPL::NPL_OK;
		}

		/**
		* activate the specified file in this runtime state. the file can be script or DLL. The function will just insert the message into the message queue and return immediately.
		* @param code: it is a chunk of code that should be executed in the destination neuron. 
		*			this code usually set the values of POL global variables.
		* @param nLength: the code length. if this is 0, length is determined from code, however, it must not exceed 4096 bytes. if it is specified. it can be any code length 
		* @param priority: bigger is higher. 0 is the default. if 1, it will be inserted to the front of the queue.  
		* @return: NPLReturnCode
		*/
		virtual NPL::NPLReturnCode Activate_async(const std::string & filepath, const char * code = NULL,int nLength=0, int priority=0) {
			return (NPL::NPLReturnCode)activate(filepath.c_str(), code, nLength);
		}

		/** same as Activate_async, except that it is a short cut name. and may be used by external dlls to activate a file on this local state asynchrounously. */
		virtual NPL::NPLReturnCode ActivateLocal(const char* filepath, const char * code = NULL,int nLength=0, int priority=0) {
			return (NPL::NPLReturnCode)activate(filepath, code, nLength);
		}

		/** same as Activate_async. except that input are read from NPLMesage. 
		* e.g.
		*	NPLMessage_ptr msg(new NPLMessage());
		*	return Activate_async(msg, priority);
		* @param msg: the caller is should only new but never delete the NPLMessage_ptr. And that the message must be created in the same thread, usually just before calling this function
		*/
		virtual NPL::NPLReturnCode Activate_async(NPL::NPLMessage_ptr& msg, int priority=0) {return NPL::NPL_OK;}

		/**
		* send a message to the current message queue. This function is rarely needed to call directly, use Activate_async instead. 
		* e.g. 
		*	NPLMessage_ptr msg(new NPLMessage());
		*	return SendMessage(msg, priority);
		* @param msg: the message to send. Please note that when the function returns, the msg will be reset to null. 
		* @return may fail if message queue is full. 
		*/
		virtual NPL::NPLReturnCode SendMessage(NPL::NPLMessage_ptr& msg, int priority=0) {return NPL::NPL_OK;}

		/** get a pointer to the current message */
		virtual const char* GetCurrentMsg(){return m_current_msg;}

		/** get length of the current message */
		virtual int GetCurrentMsgLength(){return m_current_msg_length;}

		/** get the NPL runtime environment */
		virtual NPL::INPLRuntime* GetNPLRuntime() {return m_pNPLRuntime;}

		/** write a log message
		* @param text: the content of the log message. 
		* @param nTextLen: the log text length in byte. if 0, text length will be determined automatically. 
		* @param nLogType: if this is 0, it is a normal log message. if this is 1, we will print current time, and runtime state name with the log message. 
		*/
		virtual void WriteLog(const char* text, int nTextLen=0, int nLogType = 0) {}

		//////////////////////////////////////////////////////////////////////////
		//
		// Timer functions
		//
		//////////////////////////////////////////////////////////////////////////

		/** creates a timer with the specified time-out value
		* [thread safe]
		* @param nIDEvent: Specifies a positive timer identifier. For nIDEvent<=0, they are reserved for internal uses.
		* If the NPL runtime already has a timer with the value nIDEvent, 
		* then the existing timer is replaced by the new timer. When SetTimer replaces a timer, the timer is reset. 
		* @param fElapse: Specifies the time-out value, in seconds. Please note that a timer will not be repeatedly activated if
		*		its timeout is shorter than the frame rate of the NPL simulation pipeline .
		* @param sNeuronFile: The NPL file to be activated when the time-out value elapses. For more information about the file name
		*  See NPL.activate(). 
		* @return: true if succeeds.An application can pass the value of the nIDEvent parameter to the NPL.KillTimer function to destroy the timer.
		*/
		virtual bool SetTimer(int nIDEvent, float fElapse, const char* sNeuronFile) {return false;};

		/**
		* Destroys the specified timer
		* [thread safe]
		* @param nIDEvent: Specifies the timer to be destroyed.For nIDEvent<=0, they are reserved for internal uses can not be killed by this function.
		* This value must be the same as the nIDEvent value passed to the SetTimer function that created the timer.
		* @return : If the function succeeds, the return value is true
		*/
		virtual bool KillTimer(int nIDEvent) { return false;};

		/**
		* Changes the start time and the interval between method invocations for a timer, using 32-bit signed integers to measure time intervals. 
		* [thread safe]
		* @param nIDEvent: Specifies the timer to be destroyed.For nIDEvent<=0, they are reserved for internal uses can not be killed by this function.
		* This value must be the same as the nIDEvent value passed to the SetTimer function that created the timer.
		* @param dueTime: The amount of time to delay before the invoking the callback method specified when the Timer was constructed, in milliseconds. Specify zero (0) to restart the timer immediately.
		*  however, the current implementation does not accept dueTime that is larger than MAX_TIMER_DUE_TIME	10000000, which is 10000 seconds. 
		* @param period:The time interval between invocations of the callback method specified when the Timer was constructed, in milliseconds. 
		* @return : If the function succeeds, the return value is true
		*/
		virtual bool ChangeTimer(int nIDEvent, int dueTime, int period) { return false;};

		/** function to register the a file handler in the current NPL state, so that it is callable from NPL script or C++
		* @param sFilename: any name with cpp file extension can be used. usually it is "states.cpp". The name does not need to be same as the real cpp file.
		* @param pFileHandler: if NULL it will unregister. If not, it is the file handler pointer, the pointer must be always valid, it is usually a static singleton object.
		*/
		virtual void RegisterFile(const char* sFilename, NPL::INPLActivationFile* pFileHandler = NULL) {};

		/** synchronous function call */
		virtual void call(const char * sNPLFilename, const char* sCode, int nCodeLength = 0){};
	public:
		/** process all queued message. This is usually called by the NPLMiniRuntime from the main thread. */
		virtual int Process()
		{
			int nCount = 0;
			// process as many as possible. 
			ParaEngine::Mutex::ScopedLock lock_(m_mutex);
			while(!m_input_queue.empty())
			{
				ProcessMsg(m_input_queue.front());
				m_input_queue.pop();
				++nCount;
			}
			return nCount;
		}

		/**
		* if USE_BOOST_SIGNAL_FILE_HANDLER is defined, we will allow multiple file handlers to subscribe to the same filename target. 
		* but the caller will need to link with the boost signal lib. if not defined, only one handler is allowed per file. 
		* The latest registered handler will overwrite the previous one. 
		*/
		bool RegisterFileHandler(const char* filename, const NPLFileActivateHandlerCallback_t& fileCallback)
		{
			if(filename == 0)
				return false;
			std::string sFileName = filename;

			
#ifdef USE_BOOST_SIGNAL_FILE_HANDLER
			NPLFileHandlerMap_t::iterator iter = m_file_handlers_map.find(sFileName);
			if( iter == m_file_handlers_map.end() )
			{
				m_file_handlers_map[sFileName] = boost::shared_ptr<typename NPLFileActivateHandler_t>(new NPLFileActivateHandler_t());
			}
			m_file_handlers_map[sFileName]->connect(fileCallback);
#else
			m_file_handlers_map[sFileName] = fileCallback;
#endif
			return true;
		}

	protected:
		void SetCurrentMessage(const char* msg, int nLength)
		{
			m_current_msg = msg;
			m_current_msg_length = nLength;
		}

		/** process a single message. */
		int ProcessMsg( const NPLMiniMessage& msg )
		{
			++ m_processed_msg_count;

			if(msg.m_type == 0)
			{
				NPLFileHandlerMap_t::iterator iter = m_file_handlers_map.find(msg.m_sFilename);
				if( iter!= m_file_handlers_map.end() )
				{
					CCurrentMessage  push_msg(this, msg.m_sCode.c_str(), (int)(msg.m_sCode.size()));
#ifdef USE_BOOST_SIGNAL_FILE_HANDLER
					iter->second->operator ()(ParaEngine::PluginActType_STATE, this);
#else
					iter->second(ParaEngine::PluginActType_STATE, this);
#endif
				}
			}
			return 0;
		}

	protected:

		/** pointer to the current message. it is only valid during activation call. NULL will be returned */
		const char* m_current_msg;

		/** length of the current message. it is only valid during activation call.*/
		int m_current_msg_length;

		/** the name of this runtime state. if "", it is considered an anonymous name */
		std::string m_name;

		ParaEngine::Mutex m_mutex;
		
		NPL::INPLRuntime * m_pNPLRuntime;

		/// the input message queue
		NPLMiniMessageQueue_t m_input_queue;

		/** file handlers map. */
		NPLFileHandlerMap_t m_file_handlers_map;

		/// for stats
		int m_processed_msg_count;
	};

#pragma endregion CNPLMiniState

#pragma region CNPLMiniRuntime
	/**	For full featured NPL runtime, one need to use NPL::CNPLRuntime. 

	Example usage 1:
	// derive your NPLRuntime implementation from CNPLMiniState
	class CMyNPLStateImp :  public CNPLMiniState
	{
	public:
		CMyNPLStateImp(){};
		virtual ~CMyNPLStateImp(){};
	}
	// define a NPLRuntime that uses your NPL state 
	typedef CNPLMiniRuntimeT<CMyNPLStateImp> CMyNPLRuntime;

	// Finally create CMyNPLRuntime and call Run() method at regular interval. 

	Example usage 2:
	typedef NPLInterface::CNPLMiniRuntimeT<>	CNPLMiniRuntime;
	CNPLMiniRuntime

	*/
	template <class NPL_STATE = CNPLMiniState>
	class CNPLMiniRuntimeT : public NPL::INPLRuntime
	{
	public:
		typedef boost::shared_ptr<typename NPL_STATE> NPLRuntimeState_ptr;
		typedef std::vector<NPLRuntimeState_ptr>	NPLRuntime_Temp_Pool_Type;

		/** compare functions for runtime state ptr */
		struct NPLRuntimeState_PtrOps
		{
			bool operator()( const NPLRuntimeState_ptr & a, const NPLRuntimeState_ptr & b ) const
			{ return a.get() < b.get(); }
		};
		typedef std::set <NPLRuntimeState_ptr, NPLRuntimeState_PtrOps>	NPLRuntime_Pool_Type;

		CNPLMiniRuntimeT(){
			Init();
		}
		~CNPLMiniRuntimeT(){
			Cleanup();
		}
	public:

		/** initialize NPL runtime environment */
		virtual void Init(){
			if(m_runtime_state_main.get() == 0)
			{
				// SetCompressionKey(NULL, 0, 1);
				// the default "main" runtime state
				m_runtime_state_main = CreateRuntimeState("main", NPL::NPLRuntimeStateType_NPL);
			}
		};

		/**
		* call this function regularly in the main game thread to process packages. 
		* This function also dispatches messages for the (main) runtime state if it is configured so. 
		* @param bToEnd: if true, the function will only return when there is no more input packages in the queue
		*/
		virtual void Run(bool bToEnd = true)
		{
			// the main runtime state is processed in the main game thread. 
			{
				// in case the structure is modified by other threads or during processing, we will first dump to a temp queue and then process from the queue. 
				ParaEngine::Mutex::ScopedLock lock_(m_mutex);
				NPLRuntime_Pool_Type::const_iterator iter, iter_end = m_runtime_states.end();
				for(iter = m_runtime_states.begin(); iter!=iter_end; ++iter)
				{
					m_temp_rts_pool.push_back(*iter);
				}
			}

			NPLRuntime_Temp_Pool_Type::iterator itCur, itEnd = m_temp_rts_pool.end();
			for (itCur = m_temp_rts_pool.begin(); itCur != itEnd; ++itCur)
			{
				(*itCur)->Process();
			}
			m_temp_rts_pool.clear();
		};

		/** clean up the NPL runtime environment */
		virtual void Cleanup(){
			m_runtime_state_main.reset();
			m_runtime_states.clear();
			m_active_state_map.clear();
		};

		/** whether we will process messages in the main threads in the frame move function. 
		* It is default to true;  
		* However, it is possible for server to set to false, if one wants to have a more responsive main state on the server. 
		* For example, it does high-frequency dispatcher jobs, instead of monitoring. 
		* But client application, it is only advised to set to true, otherwise the scripting and render modules will be run in different threads, leading to complexity and bugs. 
		* @NOTE: One can only call this function once to set to false. This function is only used by the ParaEngineServer
		*/
		virtual void SetHostMainStatesInFrameMove(bool bHostMainStatesInFrameMove){};

		/** create a new runtime state.
		* this function is thread safe 
		* @param name: if "", it is an anonymous runtime state. otherwise it should be a unique name. 
		* @param type_: the runtime state type. 
		* @return the newly created state is returned. If an runtime state with the same non-empty name already exist, the old one is returned. 
		*/
		virtual NPL::INPLRuntimeState* CreateState(const char* name, NPL::NPLRuntimeStateType type_=NPL::NPLRuntimeStateType_NPL){
			return CreateRuntimeState(name, type_).get();
		};

		/** get a runtime state with an explicit name.
		* this function is thread safe 
		* @param name: the name of the runtime state. if NULL or "main", the main runtime state is returned. 
		*/
		virtual NPL::INPLRuntimeState* GetState(const char* name = NULL){
			if( name == NULL)
				return m_runtime_state_main.get();

			return GetRuntimeState(name).get();
		};

		/** it get runtime state first, if none exist, it will create one and add it to the main threaded state */
		virtual NPL::INPLRuntimeState* CreateGetState(const char* name, NPL::NPLRuntimeStateType type_=NPL::NPLRuntimeStateType_NPL){
			return CreateGetRuntimeState(name, type_).get();
		};

		/** create a given runtime state.
		* this function is thread safe */
		virtual bool DeleteState(NPL::INPLRuntimeState* pRuntime_state){
			if(pRuntime_state)
			{
				return DeleteRuntimeState(((NPL_STATE*)pRuntime_state)->shared_from_this());
			}
			return false;
		};

		/** get the default (main) runtime state.*/
		virtual NPL::INPLRuntimeState* GetMainState(){
			return m_runtime_state_main.get();
		};

		/** add a given runtime state to the main game thread. 
		* this function is thread safe 
		*/
		virtual bool AddToMainThread(NPL::INPLRuntimeState* runtime_state){return false;};

		/** whether to use compression on transport layer for incoming and outgoing connections
		* @param bCompressIncoming: if true, compression is used for all incoming connections. default to false.
		* @param bCompressIncoming: if true, compression is used for all outgoing connections. default to false.
		*/
		virtual void SetUseCompression(bool bCompressIncoming, bool bCompressOutgoing){};

		/**
		* set the compression method of incoming the outgoing messages. 
		* If this is not called, the default internal key is used for message encoding. 
		* [Not Thread Safe]: one must call this function before sending or receiving any encoded messages. 
		* so it is usually called when the game engine starts. 
		* @param sKey: the byte array of key. the generic key that is used for encoding/decoding
		* @param nSize: size in bytes of the sKey. default is 64 bytes
		* @param nUsePlainTextEncoding: default to 0. 
		* if 0, the key is used as it is. 
		* if 1, the input key will be modified so that the encoded message looks like plain text(this can be useful to pass some firewalls). 
		* if -1, the input key will be modified so that the encoded message is binary. 
		*/
		virtual void SetCompressionKey(const byte* sKey=0, int nSize=0, int nUsePlainTextEncoding = 0){};

		/** Set the zlib compression level to use in case compression is enabled. 
		* default to 0, which means no compression. Compression level, which is an integer in the range of -1 to 9. 
		* Lower compression levels result in faster execution, but less compression. Higher levels result in greater compression, 
		* but slower execution. The zlib constant Z_DEFAULT_COMPRESSION, equal to -1, provides a good compromise between compression 
		* and speed and is equivalent to level 6. Level 0 actually does no compression at all, and in fact expands the data slightly 
		* to produce the zlib format (it is not a byte-for-byte copy of the input). 
		*/
		virtual void SetCompressionLevel(int nLevel){};
		virtual int GetCompressionLevel(){return 0;};

		/** set the default compression threshold for all connections on this machine. 
		* when the message size is bigger than this number of bytes, we will use m_nCompressionLevel for compression. 
		* For message smaller than the threshold, we will not compress even m_nCompressionLevel is not 0. 
		*/
		virtual void SetCompressionThreshold(int nThreshold){};
		virtual int GetCompressionThreshold(){return 0;};

		/** System level Enable/disable SO_KEEPALIVE. 
		* one needs set following values in linux procfs or windows registry in order to work as expected. 
		* - tcp_keepalive_intvl (integer; default: 75) 
		* 	The number of seconds between TCP keep-alive probes. 
		* - tcp_keepalive_probes (integer; default: 9) 
		* 	The maximum number of TCP keep-alive probes to send before giving up and killing the connection if no response is obtained from the other end. 
		* - tcp_keepalive_time (integer; default: 7200) 
		* 	The number of seconds a connection needs to be idle before TCP begins sending out keep-alive probes. Keep-alives are only sent when the SO_KEEPALIVE socket option is enabled. The default value is 7200 seconds (2 hours). An idle connection is terminated after approximately an additional 11 minutes (9 probes an interval of 75 seconds apart) when keep-alive is enabled. 
		* 	Note that underlying connection tracking mechanisms and application timeouts may be much shorter. 
		* Use the default system level TCP keep alive setting for this socket. 
		* Please see TCP keep alive for more information. It can be used to solve the "half-open connection".
		* it is arguable whether to use protocol level keep alive or implement it in the application level. 
		* @param bEnable: true to enable. 
		*/
		virtual void SetTCPKeepAlive(bool bEnable){};

		/** whether SO_KEEPALIVE is enabled. 
		* @return bEnable: true to enable.
		*/
		virtual bool IsTCPKeepAliveEnabled(){return false;};

		/** enable application level keep alive. we will use a global idle timer to detect if a connection has been inactive for GetIdleTimeoutPeriod(),
		* if so, we may send the keep alive message. 
		* @param bEnable: enable keep alive will automatically enable EnableIdleTimeout()
		*/
		virtual void SetKeepAlive(bool bEnable){};
		virtual bool IsKeepAliveEnabled(){return false;};

		/** Enable idle timeout. This is the application level timeout setting. 
		* We will create a global timer which examines all send/receive time of all open connections, if a
		* connection is inactive (idle for GetIdleTimeoutPeriod()) we will 
		*	- if IsKeepAliveEnabled() is false, actively close the connection. This is the method used by HTTP, which is the only solution to detect broken connection without sending additional keep alive message. 
		*   - if IsKeepAliveEnabled() is true, send an empty message to the other end (keep alive messages) to more accurately detect dead connections (see SetKeepAlive). 
		*/
		virtual void EnableIdleTimeout(bool bEnable){};
		virtual bool IsIdleTimeoutEnabled(){return false;};

		/** how many milliseconds of inactivity to assume this connection should be timed out. if 0 it is never timed out. */
		virtual void SetIdleTimeoutPeriod(int nMilliseconds){};
		virtual int GetIdleTimeoutPeriod(){return false;};

		/**
		* start the NPL net server's io_service loop. This function returns immediately. it will spawn the accept and dispatcher thread.  
		* call this function only once per process.
		* @param server: default to "127.0.0.1"
		* @param port: default to "60001"
		*/
		virtual void StartNetServer(const char* server=NULL, const char* port=NULL){};

		/** stop the net server */
		virtual void StopNetServer(){};

		/** add a nID, filename pair to the public file list. 
		* we only allow remote NPL runtime to activate files in the public file list. 
		* Each public file has a user defined ID. The NPL network layer always try to use its ID for transmission to minimize bandwidth. 
		* There are some negotiations between sender and receiver to sync the string to ID map before they use it. 
		* [thread safe]
		* @param nID: the integer to encode the string. it is usually small and positive number.
		* @param sString: the string for the id. if input is empty, it means removing the mapping of nID. 
		*/
		virtual void AddPublicFile(const std::string& filename, int nID){};

		/** clear all public files, so that the NPL server will be completely private. 
		* [thread safe]
		*/
		virtual void ClearPublicFiles(){};

		/** get the ip address of given NPL connection. 
		* this function is usually called by the server for connected clients. 
		* @param nid: nid or tid. 
		* @param pOutPut: it must be at least [256] bytes big, that receives the output. 
		* @return: the ip address in dot format. empty string is returned if connection can not be found. 
		*/
		virtual void GetIP(const char* nid, char* pOutput){};


		/** accept a given connection. The connection will be regarded as authenticated once accepted. 
		* [thread safe]
		* @param tid: the temporary id or NID of the connection to be accepted. usually it is from msg.tid or msg.nid. 
		* @param nid: if this is not nil, tid will be renamed to nid after accepted. 
		*/
		virtual void accept(const char* tid, const char* nid = NULL){};

		/** reject and close a given connection. The connection will be closed once rejected. 
		* [thread safe]
		* @param nid: the temporary id or NID of the connection to be rejected. usually it is from msg.tid or msg.nid. 
		*/
		virtual void reject(const char* nid, int nReason = 0){};


		//////////////////////////////////////////////////////////////////////////
		//
		// jabber client functions
		//
		//////////////////////////////////////////////////////////////////////////

		/**
		* get an existing jabber client instance interface by its JID.
		* If the client is not created using CreateJabberClient() before, function may return NULL.
		* @param sJID: such as "lixizhi@paraweb3d.com"
		*/
		virtual ParaEngine::INPLJabberClient* GetJabberClient(const char* sJID){return NULL;};
		/**
		* Create a new jabber client instance with the given jabber client ID. It does not open a connection immediately.
		* @param sJID: such as "lixizhi@paraweb3d.com"
		*/
		virtual ParaEngine::INPLJabberClient* CreateJabberClient(const char* sJID){return NULL;};

		/**
		* close a given jabber client instance. Basically, there is no need to close a web service, 
		* unless one wants to reopen it with different credentials
		* @param sJID: such as "lixizhi@paraweb3d.com"
		*/
		virtual bool CloseJabberClient(const char* sJID){return false;};

		//////////////////////////////////////////////////////////////////////////
		//
		// new libcUrl interface. 
		//
		//////////////////////////////////////////////////////////////////////////

		/** Append URL request to a pool. 
		* @param pUrlTask: must be new CURLRequestTask(), the ownership of the task is transfered to the manager. so the caller should never delete the pointer. 
		* @param sPoolName: the request pool name. If the pool does not exist, it will be created. If null, the default pool is used. 
		*/
		virtual bool AppendURLRequest(ParaEngine::CURLRequestTask* pUrlTask, const char* sPoolName = NULL){return false;};

		/**
		* There is generally no limit to the number of requests sent. However, each pool has a specified maximum number of concurrent worker slots. 
		*  the default number is 1. One can change this number with this function. 
		*/
		virtual bool ChangeRequestPoolSize(const char* sPoolName, int nCount){return false;};


		//////////////////////////////////////////////////////////////////////////
		//
		// Downloader functions
		//
		//////////////////////////////////////////////////////////////////////////

		/**
		* Asynchronously download a file from the url.
		* @param callbackScript: script code to be called, a global variable called msg is assigned, as below
		*  msg = {DownloadState=""|"complete"|"terminated", totalFileSize=number, currentFileSize=number, PercentDone=number}
		*/
		virtual void AsyncDownload(const char* url, const char* destFolder, const char* callbackScript, const char* DownloaderName){};


		/**
		* cancel all asynchronous downloads that matches a certain downloader name pattern
		* @param DownloaderName:regular expression. such as "proc1", "proc1.*", ".*"
		*/
		virtual void CancelDownload(const char* DownloaderName){};

		/**
		* Synchronous call of the function AsyncDownload(). This function will not return until download is complete or an error occurs. 
		* this function is rarely used. AsyncDownload() is used. 
		* @return:1 if succeed, 0 if fail
		*/
		virtual int Download(const char* url, const char* destFolder, const char* callbackScript, const char* DownloaderName){return 0;};

		/**
		* add a DNS server record to the current NPL runtime.
		* DNS server record is a mapping from name to (IP:port)
		* if one maps several IP:port to the same name, the former ones will be overridden.
		* @param sDNSName: the DNS server name. the DNS name "_world" is used for the current 
		*	world DNS server. It is commonly used as a DNS reference to the current world that 
		*	the user is exploring.
		* @param sAddress: "IP:port". e.g. "192.168.1.10:4000"
		*/
		virtual void NPL_AddDNSRecord(const char * sDNSName, const char* sAddress){};

		/**
		* Set the default channel ID, default value is 0. Default channel is used when NPL.activate() call¡¯s does not contain the channel property.
		* @param channel_ID It can be a number in [0,15].default is 0
		*/
		virtual void NPL_SetDefaultChannel(int channel_ID){};
		/**
		* Get the default channel ID, default value is 0. Default channel is used when NPL.activate() call¡¯s does not contain the channel property.
		* @return channel_ID It can be a number in [0,15].default is 0
		*/
		virtual int NPL_GetDefaultChannel(){return 0;};

		/**
		* Messages can be sent via predefined channels. There are 16 channels from 0 to 15 to be used. 0 is the default channel. 
		* This method sets the channel property for a given channel. The default channel property is given in table.
		The following table shows the default NPL channel properties. It is advised for users to stick to this default mapping when developing their own applications. 
		Table 1. 	Default NPL channel properties
		channel_ID	Priority	Reliability				Usage
		0		med			RELIABLE_ORDERED		System message
		1		med			UNRELIABLE_SEQUENCED	Character positions
		2		med			RELIABLE_ORDERED		Large Simulation Object transmission, such as terrain height field.
		4		med			RELIABLE_ORDERED		Chat message
		14		med			RELIABLE				files transmission and advertisement
		15		med			RELIABLE_SEQUENCED		Voice transmission
		11-15	med			RELIABLE_ORDERED	

		* @param channel_ID 
		* @param priority 
		* @param reliability 
		*/
		virtual void NPL_SetChannelProperty(int channel_ID, int priority, int reliability){};
		/**
		* reset all 16 predefined channel properties. according to table1. Default NPL channel properties. see also NPL_SetChannelProperty
		The following table shows the default NPL channel properties. It is advised for users to stick to this default mapping when developing their own applications. 
		Table 1. 	Default NPL channel properties
		channel_ID	Priority	Reliability				Usage
		0		med			RELIABLE_ORDERED		System message
		1		med			UNRELIABLE_SEQUENCED	Character positions
		2		med			RELIABLE_ORDERED		Large Simulation Object transmission, such as terrain height field.
		4		med			RELIABLE_ORDERED		Chat message
		14		med			RELIABLE				files transmission and advertisement
		15		med			RELIABLE_SEQUENCED		Voice transmission
		11-15	med			RELIABLE_ORDERED	
		*/
		virtual void NPL_ResetChannelProperties(){};

		/**
		* see also NPL_SetChannelProperty
		* @param channel_ID 
		* @param priority [out]
		* @param reliability [out]
		*/
		virtual void NPL_GetChannelProperty(int channel_ID, int* priority, int* reliability){};

		//////////////////////////////////////////////////////////////////////////
		//
		// Global activation functions
		//
		//////////////////////////////////////////////////////////////////////////

		/**
		* activate the specified file. file name is used as message target. 
		* [thread safe] This function is thread safe, if and only if pRuntimeState is still valid
		* @note: pure data table is defined as table consisting of only string, number and other table of the above type. 
		*   NPL.activate function also accepts ParaFileObject typed message data type. ParaFileObject will be converted to base64 string upon transmission. There are size limit though of 10MB.
		*   one can also programmatically check whether a script object is pure date by calling NPL.SerializeToSCode() function. Please note that data types that is not pure data in sCode will be ignored instead of reporting an error.
		*/
		virtual int Activate(NPL::INPLRuntimeState* pRuntimeState, const char * sNeuronFile, const char * code = NULL,int nLength=0,  int channel=0, int priority=2, int reliability=3){
			if(sNeuronFile == NULL)
			{
				return (int)NPL::NPL_FailedToLoadFile;
			}

			NPLFileName FullName(sNeuronFile);

			// use Dispatcher to dispatch to a proper local runtime state or a remote one. 
			if(pRuntimeState == 0)
			{
				// default to main state. 
				return m_runtime_state_main->Activate_async(FullName.sRelativePath, code, nLength);
			}
			else 
			{
				if(FullName.sNID.empty())
				{
					// local activation between local npl runtime state. 
					if(!FullName.sRuntimeStateName.empty())
					{
						NPLRuntimeState_ptr rts = GetRuntimeState(FullName.sRuntimeStateName);
						if(rts.get() != 0)
						{
							return rts->Activate_async(FullName.sRelativePath, code, nLength);
						}
						else
						{
							return -1;
						}
					}
					else
					{
						return pRuntimeState->Activate_async(FullName.sRelativePath, code, nLength);
					}
				}
				else
				{
					return (int)NPL::NPL_Error;
				}
			}
		}
	public:
		/** create a new runtime state.
		* this function is thread safe 
		* @param name: if "", it is an anonymous runtime state. otherwise it should be a unique name. 
		* @param type_: the runtime state type. 
		* @return the newly created state is returned. If an runtime state with the same non-empty name already exist, the old one is returned. 
		*/
		NPLRuntimeState_ptr CreateRuntimeState(const std::string& name, NPL::NPLRuntimeStateType type_=NPL::NPLRuntimeStateType_NPL)
		{
			NPLRuntimeState_ptr runtimestate = GetRuntimeState(name);
			if(runtimestate.get() == 0)
			{
				runtimestate.reset(new NPL_STATE(name.c_str()));
				runtimestate->Init();
				ParaEngine::Mutex::ScopedLock lock_(m_mutex);
				m_runtime_states.insert(runtimestate);
				// assert(m_runtime_states.find(runtimestate)!= m_runtime_states.end());
				if(!name.empty())
					m_active_state_map[name] = runtimestate;
			}
			return runtimestate;
		}

		/** get a runtime state with an explicit name.
		* this function is thread safe 
		* @param name: the name of the runtime state. if empty or "main", the main runtime state is returned. 
		*/
		NPLRuntimeState_ptr GetRuntimeState(const std::string& name)
		{
			if( name.empty() )
				return m_runtime_state_main;

			ParaEngine::Mutex::ScopedLock lock_(m_mutex);
			std::map<string, NPLRuntimeState_ptr>::iterator iter = m_active_state_map.find(name);

			if( iter != m_active_state_map.end())
			{
				return iter->second;
			}
			return NPLRuntimeState_ptr();
		}

		/** it get runtime state first, if none exist, it will create one and add it to the main threaded state */
		NPLRuntimeState_ptr CreateGetRuntimeState(const std::string& name, NPL::NPLRuntimeStateType type_=NPL::NPLRuntimeStateType_NPL)
		{
			NPLRuntimeState_ptr runtimestate =  GetRuntimeState(name);
			if(runtimestate.get() == 0)
			{
				// create the state and run it in the main thread. 
				runtimestate =  CreateRuntimeState(name, type_);
			}
			return runtimestate;
		}

		/** create a given runtime state.
		* this function is thread safe */
		bool DeleteRuntimeState(NPLRuntimeState_ptr runtime_state)
		{
			if(runtime_state.get() == 0)
				return true;
			ParaEngine::Mutex::ScopedLock lock_(m_mutex);
			NPLRuntime_Pool_Type::iterator iter = m_runtime_states.find(runtime_state);
			if(iter != m_runtime_states.end())
			{
				m_runtime_states.erase(iter);
				return true;
			}
			if( ! runtime_state->GetName().empty() )
			{
				m_active_state_map.erase(runtime_state->GetName());
			}
			return false;
		}

		/** get the default (main) runtime state.*/
		NPLRuntimeState_ptr GetMainRuntimeState(){
			return m_runtime_state_main;
		}
		
	protected:
		/// the default (main) NPL runtime state. 
		NPLRuntimeState_ptr m_runtime_state_main;

		/// all NPL runtime states in the NPL runtime
		NPLRuntime_Pool_Type m_runtime_states;

		/// temporary run time states queue
		NPLRuntime_Temp_Pool_Type m_temp_rts_pool;

		/** a mapping from the runtime state name to runtime state instance pointer */
		std::map<std::string, NPLRuntimeState_ptr> m_active_state_map;

		ParaEngine::Mutex m_mutex;
		ParaEngine::Semaphore m_semaphore;
	};
#pragma endregion CNPLMiniRuntime
}