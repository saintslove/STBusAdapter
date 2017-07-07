//
// Created by wangqi on 17-6-7.
//

#include "STBusAdapter.h"
#include <sstream>
#include <boost/bind.hpp>
#include "Base.h"


STBusAdapter::STBusAdapter(const std::string &config_path,int nDevType, const std::string& strDevSN)
         :_config_path(config_path)
         ,initMsgSending(true)
         ,RecvDataHandler(-1)
         ,SendDataHandler(-1)
         ,MonitorHandler(-1)
         ,timebuckets_lock()
         ,Map_Monitor_hasConnectrd_lock()
         ,list_map_lock()
         ,map_conf_lock()
         ,Map_RecvData_hasConnected_lock()
         ,Map_RecvData_hasConnected_Condition(Map_RecvData_hasConnected_lock)
         ,day_time(muduo::TimeZone::toUtcTime(muduo::Timestamp::now().secondsSinceEpoch()))
         ,RecvDataServerSatrtTime()
         ,loadconfig_thread(boost::bind(&STBusAdapter::LoadConfig,this),"loadconfig_thread")
         ,sendInitMsg_thread(boost::bind(&STBusAdapter::SendInitMsg,this),"sendInitMsg_thread")
         ,list_map()
         ,map_conf()
         ,map_count(NULL)
         ,map_monitor_connected()
         ,map_RecvData_hasConnected()
         ,timebuckets()
         ,msg_queue()
         ,local_msg_queue(msg_queue)
{
    map_count = Count::getInstance();
    loadconfig_thread.start();
    sendInitMsg_thread.start();
    BATNetSDK_Init(nDevType, const_cast<char*>(strDevSN.c_str()),true);
};


STBusAdapter::~STBusAdapter()
{
    initMsgSending = false;
    loadconfig_thread.join();
    sendInitMsg_thread.join();
    if(RecvDataHandler != -1)
    {
        BATNetSDKRaw_DeleteObj(RecvDataHandler);
    }
    if(SendDataHandler != -1)
    {
        BATNetSDKRaw_DeleteObj(SendDataHandler);
    }
    if(MonitorHandler != -1)
    {
        BATNetSDKRaw_DeleteObj(MonitorHandler);
    }
    BATNetSDK_Release();
}


void STBusAdapter::Insert(XMLNode& node,Map_list& map_list)
{
    int num = 0;
    int nodenum = node.nChildNode();
    std::string nodename(node.getName());
    if(!node.isEmpty())
    {
        while(num < nodenum)
        {
            XMLNode xIP = node.getChildNode("IP",num++);
            const char* context = xIP.getText();
            if(xIP.nText() > 0)
            {
                if(map_list[nodename].end() == std::find_if(map_list[nodename].begin(),map_list[nodename].end(),find_item<std::string,std::string,false>(std::string(context))))
                {
                    muduo::MutexLockGuard lock(list_map_lock);
                    map_list[nodename].push_back(context);
                }

            }
        }
    }
}
void STBusAdapter::LoadConfig()
{
    int count;
    XMLResults result;
    XMLNode xRoot = XMLNode::parseFile(_config_path.c_str(), NULL, &result);
    LOG_INFO<<XMLNode::getError(result.error)<<"\tLine = "<<result.nLine<<"\tColumn = "<<result.nColumn;

    if(!xRoot.isEmpty())
    {
        XMLNode xMainNode = xRoot.getChildNode("List");
        uint8_t nNode = xMainNode.nChildNode();
        while(count < nNode)
        {
            XMLNode  xData = xMainNode.getChildNode(count++);
            XMLCSTR nodeName = xData.getName();
            if(!strcmp(nodeName,std::string("admin").c_str()) || !strcmp(nodeName,std::string("all").c_str()) ||!strcmp(nodeName,std::string("gps").c_str()) ||
                    !strcmp(nodeName,std::string("oper").c_str()) || !strcmp(nodeName,std::string("warning").c_str()))
            {
                Insert(xData,list_map);
            }
            else
            {
                if(map_conf.find(nodeName) == map_conf.end() || strcmp(map_conf[nodeName].c_str(),xData.getText()) != 0)
                {
                    muduo::MutexLockGuard lock(map_conf_lock);
                    map_conf[nodeName] = xData.getText();
                }
            }
        }
    }
}


int STBusAdapter::StartRecvDataServer()
{
    RecvDataServerSatrtTime = TimeZone::toUtcTime(Timestamp::now().secondsSinceEpoch());
    CCMS_NETADDR addr = { {0},0};
    {
        muduo::MutexLockGuard lock(map_conf_lock);
        std::istringstream is(map_conf["recv_port"]);
        is >> addr.nPort;
    }
    RecvDataHandler = BATNetSDKRaw_CreateServerObj(&addr);
    BATNetSDKRaw_SetMsgCallBack(RecvDataHandler,OnRecvDatahand,this);
    BATNetSDKRaw_SetConnCallBack(RecvDataHandler,OnRecvDataConnectCallback,this);
    BATNetSDKRaw_Start(RecvDataHandler);
    return 0;
}


int STBusAdapter::StartSendDataServer()
{
    CCMS_NETADDR addr = { {0},0};
    {
        muduo::MutexLockGuard lock(map_conf_lock);
        std::istringstream is(map_conf["send_port"]);
        is >> addr.nPort;
    }
    SendDataHandler = BATNetSDKRaw_CreateServerObj(&addr);
    BATNetSDKRaw_SetMsgCallBack(SendDataHandler,OnSendDatahand,this);
    BATNetSDKRaw_SetConnCallBack(SendDataHandler,OnSendDataConnectCallback,this);
    BATNetSDKRaw_Start(SendDataHandler);
    return 0;
}

int STBusAdapter::StartMonitorServer()
{
    CCMS_NETADDR addr = { {0},0};
    {
        muduo::MutexLockGuard lock(map_conf_lock);
        std::istringstream is(map_conf["monitoring_port"]);
        is >> addr.nPort;
    }
    MonitorHandler = BATNetSDKRaw_CreateServerObj(&addr);
    BATNetSDKRaw_SetMsgCallBack(MonitorHandler,OnMonitorDatahand,this);
    BATNetSDKRaw_SetConnCallBack(MonitorHandler,OnMonitorDataConnectCallback,this);
    BATNetSDKRaw_Start(MonitorHandler);
    return 0;
}


int STBusAdapter::OnRecvDatahand(int sessionId, const char *buf, int len, void *userdata)
{
    STBusAdapter* that = reinterpret_cast<STBusAdapter*>(userdata);
    return that->RecvDatahand(sessionId,buf,len,userdata);
}


int STBusAdapter::OnRecvDataConnectCallback(int sessionId, int status, const char *ip, unsigned short port,
                                            void *userdata)
{
    STBusAdapter* that = reinterpret_cast<STBusAdapter*>(userdata);
    return that->RecvDataConnectCallback(sessionId,status,ip,port,userdata);
}


int STBusAdapter::OnSendDatahand(int sessionId, const char *buf, int len, void *userdata)
{
    STBusAdapter* that = reinterpret_cast<STBusAdapter*>(userdata);
    return that->SendDatahand(sessionId,buf,len,userdata);
}


int STBusAdapter::OnSendDataConnectCallback(int sessionId, int status, const char *ip, unsigned short port,
                                            void *userdata)
{
    STBusAdapter* that = reinterpret_cast<STBusAdapter*>(userdata);
    return that->SendDataConnectCallback(sessionId,status,ip,port,userdata);
}


int STBusAdapter::OnMonitorDatahand(int sessionId, const char *buf, int len, void *userdata)
{
    STBusAdapter* that = reinterpret_cast<STBusAdapter*>(userdata);
    return that->MonitorDatahand(sessionId,buf,len,userdata);
}


int STBusAdapter::OnMonitorDataConnectCallback(int sessionId, int status, const char *ip, unsigned short port,
                                            void *userdata)
{
    STBusAdapter* that = reinterpret_cast<STBusAdapter*>(userdata);
    return that->MonitorDataConnectCallback(sessionId,status,ip,port,userdata);
}


int STBusAdapter::RecvDatahand(int sessionId, const char *buf, int len, void *userdata)
{
//    LOG_INFO << "recv\t" << len << "\tbytes data ,sessionID => " << sessionId;
    uint8_t head_flag;
    uint8_t end_flag;
    uint16_t data_len;
    uint16_t msg_type;
    uint8_t GPSCenterID;
    const char* begin = buf;
    const char* end = buf + len;
    if(len < 3)
    {
        LOG_INFO << "msg not complete!";
        return 0;
    }
    DECODE(&head_flag,const_cast<char**>(&buf),end - buf, false);
    DECODE(&data_len,const_cast<char**>(&buf),end - buf,true);
    boost::shared_ptr<char> data = boost::shared_ptr<char>(new char[data_len],Deleter<char>());
    if(len < data_len)
    {
        LOG_INFO << "msg not complete!";
        return 0;
    }
    if(head_flag != 0x7e)
    {
        LOG_INFO << "head_flag != 0x7e,msg_error";
        map_count->increment("misunderstand_count");
        Count_day_misunderstand();
        return data_len;
    }
    if(len - 4 < 3)
    {
        LOG_INFO << "msg error,data_len < 3 ,data_len => " << len - 3;
        map_count->increment("misunderstand_count");
        Count_day_misunderstand();
        return data_len;
    }
    DECODE(&msg_type,const_cast<char**>(&buf),end - buf, true);
    DECODE(&GPSCenterID,const_cast<char**>(&buf),end - buf, false);
    DECODEARR(data.get(),data_len - 7,const_cast<char**>(&buf),end - buf);
    DECODE(&end_flag,const_cast<char**>(&buf),end - buf, false);
    if(end_flag != 0x23)
    {
        LOG_INFO << "end_flag != 0x23,msg_error";
        map_count->increment("misunderstand_count");
        Count_day_misunderstand();
        return data_len;
    }
    if(msg_type == 0)
    {
        LOG_INFO << "get LOGIN_RANDOM_SERIAL message.";
    }
    else if(msg_type == 0x1001)
    {
        LOG_INFO << "get LOGIN_REQ message.";
        msg_type = 0x9001;
        std::string msg_data = std::string();
        uint32_t dest_len = 7;
        boost::shared_ptr<char> dest = boost::shared_ptr<char>(new char[7],Deleter<char>());
        FormatMsg(msg_type,msg_data.c_str(),0,dest.get(),dest_len);
        BATNetSDKRaw_Send(RecvDataHandler,sessionId,dest.get(),7);
    }
    else if(msg_type == 0x9001)
    {
        LOG_INFO << "get LOGIN_RSP message.";
    }
    else if(msg_type == 0x1002)
    {
        LOG_INFO << "get LOGOUT_REQ message.";
        msg_type = 0x9002;
        std::string msg_data = std::string();
        uint32_t dest_len = 7;
        boost::shared_ptr<char> dest = boost::shared_ptr<char>(new char[7],Deleter<char>());
        FormatMsg(msg_type,msg_data.c_str(),0,dest.get(),dest_len);
        BATNetSDKRaw_Send(RecvDataHandler,sessionId,dest.get(),7);
    }
    else if(msg_type == 0x9002)
    {
        LOG_INFO << "get LOGOUT_RSP message.";
    }
    else if(msg_type == 0x1003)
    {
        LOG_INFO << "get LINKTEST_REQ message.";
        msg_type = 0x9003;
        std::string msg_data = std::string();
        uint32_t dest_len = 7;
        boost::shared_ptr<char> dest = boost::shared_ptr<char>(new char[7],Deleter<char>());
        FormatMsg(msg_type,msg_data.c_str(),0,dest.get(),dest_len);
        BATNetSDKRaw_Send(RecvDataHandler,sessionId,dest.get(),7);
    }
    else if(msg_type == 0x9003)
    {
        LOG_INFO << "get LINKTEST_RSP message.";
    }
    else if(msg_type == 0x1004)
    {
        LOG_INFO << "get APPLY_REQ message.";
    }
    else if(msg_type == 0x9004)
    {
        LOG_INFO << "get APPLY_RSP message.";
    }
    else if(msg_type == 0x0001)
    {
        LOG_INFO << "get DELIVER message.";
        map_count->increment("total_count");
        Count_day_total_count();
        uint16_t buf_len = data_len - 2;
        boost::shared_ptr<char> dest = boost::shared_ptr<char>(new char[buf_len],Deleter<char>());
        char* dest_buf = dest.get();
        const char* end = dest.get() + buf_len;
        ENCODE(&msg_type,&dest_buf,end - dest_buf,true);
        ENCODE(&buf_len,&dest_buf,end - dest_buf,true);
        ENCODE(&GPSCenterID,&dest_buf,end - dest_buf, false);
        ENCODEARR(data.get(),data_len - 7,&dest_buf,end - dest_buf);
        msg_queue.push(std::make_pair(buf_len,dest));
    }
    return data_len;
}


int STBusAdapter::RecvDataConnectCallback(int sessionId, int status, const char *ip, unsigned short port,
                                          void *userdata)
{
    connnect_timebucket timebucket;
    LOG_INFO << "RecvDataConnectCallback   sessionId => "<< sessionId<<"\tip => " << ip << "\tport => " << port <<"\tstatus => "<< status;
    if(!status)
    {
        {
            muduo::MutexLockGuard lock(map_conf_lock);
            std::string ip_str = std::string(ip);
            std::size_t pos = ip_str.find_last_of('.');
            std::istringstream is(map_conf["DS_pattern"]);
            if(strcmp(ip_str.substr(0,pos + 1).c_str(),is.str().c_str()))
            {
                LOG_INFO << "Recv port connection " << ip << "will be closed,because it seem not " << map_conf["DS_unit"];
                BATNetSDKRaw_Disconnect(RecvDataHandler,sessionId);
            }
        }
        {
            muduo::MutexLockGuard lock(Map_RecvData_hasConnected_lock);
            if(map_RecvData_hasConnected.end() == std::find_if(map_RecvData_hasConnected.begin(),map_RecvData_hasConnected.end(),find_item<Map_RecvData_hasConnected_type,std::string,true>(std::string(ip))))
            {
                map_RecvData_hasConnected[ip] = {sessionId,status,false};
                Map_RecvData_hasConnected_Condition.notify();
            }
        }
        int num;
        map_count->increment("recv_max_conn");
        {
            muduo::MutexLockGuard lock(map_conf_lock);
            std::istringstream in(map_conf["recv_max_conn"]);
            in >> num;
        }
        if(map_count->get("recv_max_conn") > num)
        {
            LOG_INFO << "RecvDataServer overload,has over recv_max_conn : 1";
            BATNetSDKRaw_Disconnect(RecvDataHandler,sessionId);
            map_count->decrement("recv_max_conn");
        }
        timebucket = {sessionId,false,TimeZone::toUtcTime(Timestamp::now().secondsSinceEpoch()),{0}};
        muduo::MutexLockGuard lock(timebuckets_lock);
        timebuckets.push_back(timebucket);
    }
    else
    {
        {
            muduo::MutexLockGuard lock(Map_RecvData_hasConnected_lock);
            if(map_RecvData_hasConnected.end() != std::find_if(map_RecvData_hasConnected.begin(),map_RecvData_hasConnected.end(),find_item<Map_RecvData_hasConnected_type,std::string,true>(std::string(ip))))
            {
                map_RecvData_hasConnected.erase(ip);
                LOG_INFO << "IP => " << ip << "\tPort => " << port << "\tDisconnected";
            }
        }
        map_count->decrement("recv_max_conn");
        muduo::MutexLockGuard lock(timebuckets_lock);
        timebuckets_Iter start = timebuckets.begin();
        while((start++) != timebuckets.end())
        {
             if(start->sessionID == sessionId && !start->closed)
             {
                 start->closed = true;
                 start->connect_close = TimeZone::toUtcTime(Timestamp::now().secondsSinceEpoch());
             }
        }
    }
}

void STBusAdapter::FormatMsg(uint16_t& msg_type, const char* source_data, uint32_t source_len,char* dest,uint32_t& dest_len)
{
    uint8_t head_flag = 0x7e;
    uint8_t end_flag = 0x23;
    uint8_t GPSCenterID = 'B';
    const char* begin = dest;
    const char* end = begin + dest_len;
    boost::shared_ptr<char> data = boost::shared_ptr<char>(new char[source_len],Deleter<char>());
    memcpy(data.get(),source_data,source_len);
    uint16_t len = (uint16_t)(source_len + 7);
    ENCODE(&head_flag,&dest,end - dest, false);
    ENCODE(&len,&dest,end - dest,true);
    ENCODE(&msg_type,&dest,end - dest,true);
    ENCODE(&GPSCenterID,&dest,end - dest, false);
    ENCODEARR(data.get(),source_len,&dest,end - dest);
    ENCODE(&end_flag,&dest,end - dest, false);
    dest_len = dest - begin;
}

void STBusAdapter::InitMsg(boost::shared_ptr<char>& buf)
{
    uint16_t msg_type = 0;
    msg_type = ntohs(msg_type);
    std::string random_string("1234567890");
    uint32_t buflen = random_string.length() + 7;
    FormatMsg(msg_type,random_string.c_str(),uint32_t(random_string.length()),buf.get(),buflen);
    LOG_INFO << "initMessage len => " << buflen << "\tdata_len => " << random_string.length() << "\tdata => " << random_string;
}

void STBusAdapter::Count_day_total_count()
{
    struct tm current_time = TimeZone::toUtcTime(Timestamp::now().secondsSinceEpoch());
    if((current_time.tm_year - day_time.tm_year) > 1 || (current_time.tm_yday - day_time.tm_yday) > 1 )
    {
        day_time = current_time;
        int32_t before_day_total_count = map_count->getAndSet("day_total_count",0);
        int32_t before_day_misunderstand_count = map_count->getAndSet("day_misunderstand_count",0);
        LOG_INFO << "before_day_total_count => " << before_day_total_count << "\tbefore_day_misunderstand_count => " << before_day_misunderstand_count;

    }
    map_count->increment("day_total_count");
}


void STBusAdapter::Count_day_misunderstand()
{
    struct tm current_time = TimeZone::toUtcTime(Timestamp::now().secondsSinceEpoch());
    if((current_time.tm_year - day_time.tm_year) > 1 || (current_time.tm_yday - day_time.tm_yday) > 1)
    {
        day_time = current_time;
        int32_t before_day_misunderstand_count = map_count->getAndSet("day_misunderstand_count",0);
        int32_t before_day_total_count = map_count->getAndSet("day_total_count",0);
        LOG_INFO << "before_day_total_count => " << before_day_total_count << "\tbefore_day_misunderstand_count => " << before_day_misunderstand_count;
    }
    map_count->increment("day_misunderstand_count");
}


void STBusAdapter::Getline(const char* buf,std::string& cmd,int buflen,int& len)
{
    std::istringstream read(std::string(buf,buf + buflen));
    std::getline(read,cmd);
    len = cmd.length() + 1;
}


void STBusAdapter::GetCommand(const char *buf, std::vector<std::string>& cmd, int buflen, int &len)
{
    std::string cmd_vec;
    std::istringstream read(std::string(buf,buf + buflen));
    std::getline(read,cmd_vec);
    std::size_t begin = 0;
    std::size_t pos = cmd_vec.find_first_of(':');
    while(pos != std::string::npos)
    {
        std::string tmp = cmd_vec.substr(begin,pos - begin);
        cmd.push_back(tmp);
        begin = pos + 1;
        pos = cmd_vec.find_first_of(":",begin);
    }
    std::string tmp = cmd_vec.substr(begin,cmd_vec.length() - begin + 1);
    cmd.push_back(tmp);
    len = cmd_vec.length() + 1;
}

int STBusAdapter::SendDatahand(int sessionId, const char *buf, int len, void *userdata)
{
    int pwd_length;
    std::string pwd;
    Getline(buf,pwd,len,pwd_length);
    muduo::MutexLockGuard lock(map_conf_lock);
    if(pwd.compare(map_conf["client_pwd"] + "\r"))
    {
        LOG_INFO << "sessionID => "<<sessionId << "\tcan not be verify, RS_server will close the connection.";
        std::string reply = "password or needTpye => " + pwd + "\tcan not be verify, RS_server will close the connection.";
        BATNetSDKRaw_Send(SendDataHandler,sessionId,reply.c_str(),reply.length());
        BATNetSDKRaw_Disconnect(SendDataHandler,sessionId);
        map_count->decrement("send_max_conn");
    }
    LOG_INFO << "sessionID => "<<sessionId << "\tverify success.";
    return pwd_length;
}


int STBusAdapter::SendDataConnectCallback(int sessionId, int status, const char *ip, unsigned short port,
                                          void *userdata)
{
    LOG_INFO << "SendDataConnectCallback   sessionId => "<< sessionId<<"\tip => " << ip << "\tport => " << port <<"\tstatus => "<< status;
    if(!status)
    {
        int send_max_num,max_num;
        map_count->increment("send_max_conn");
        {
            muduo::MutexLockGuard lock(map_conf_lock);
            std::istringstream send_in(map_conf["send_max_conn"]);
            std::istringstream max_in(map_conf["G_max_count"]);
            send_in >> send_max_num;
            max_in >> max_num;
        }
        {
            muduo::MutexLockGuard lock(list_map_lock);
            if(list_map["all"].end() == std::find_if(list_map["all"].begin(),list_map["all"].end(),find_item<std::string,std::string,true>(std::string(ip))))
            {
                LOG_INFO << "ip => " << ip << "\tport => " << port <<"\twill be closed due to not in all_list.";
                BATNetSDKRaw_Disconnect(SendDataHandler,sessionId);
                map_count->decrement("send_max_conn");
            }
        }
        if(map_count->get("send_max_conn") > send_max_num)
        {
            LOG_INFO << "SendDataServer overload,has over send_max_conn:50";
            BATNetSDKRaw_Disconnect(SendDataHandler,sessionId);
            map_count->decrement("send_max_conn");
        }
        Thread_local_msg msg = {SendDataHandler,max_num,port,sessionId,0};
        local_msg_queue.add_ip(ip,msg);
    }
    else
    {
        local_msg_queue.delete_ip(ip);
        map_count->decrement("send_max_conn");
    }
}


int STBusAdapter::SendInitMsg()
{
    while(initMsgSending)
    {
        {
            muduo::MutexLockGuard lock(Map_RecvData_hasConnected_lock);
            Map_RecvData_hasConnected_Iter it = std::find_if(map_RecvData_hasConnected.begin(),map_RecvData_hasConnected.end(),find_item<Map_RecvData_hasConnected_type,std::string,false>());
            if(map_RecvData_hasConnected.end() == it)
            {
                Map_RecvData_hasConnected_Condition.waitForMillSeconds(10);
                continue;
            }
            boost::shared_ptr<char> init_msg = boost::shared_ptr<char>(new char[17],Deleter<char>());
            InitMsg(init_msg);
            if(!BATNetSDKRaw_Send(RecvDataHandler,it->second.SessionID,init_msg.get(),17))
            {
                LOG_INFO << "Send Init Msg Success!";
            }
            it->second.Send_Init_Msg = true;
        }
    }
}


int STBusAdapter::MonitorDatahand(int sessionId, const char *buf, int len, void *userdata)
{
    LOG_INFO << "MonitorDatahand get msg from sessionID => " << sessionId;
    Map_Monitor_hasConnectrd_Iter it;
    {
        muduo::MutexLockGuard lock(Map_Monitor_hasConnectrd_lock);
        it = std::find_if(map_monitor_connected.begin(),map_monitor_connected.end(),find_item<Map_Monitor_hasConnectrd_type,int,false>(sessionId));
    }
    if(map_monitor_connected.end() != it)
    {
        LOG_INFO << "MonitorDataServer get msg from client => " << it->first << "\tport => " << it->second.Port;
    }
    else
    {
        LOG_INFO << "client => " << it->first << "\tport => " << it->second.Port << "\thas be closed!";
        return 0;
    }
    std::string end_flag = "pxb_dataguard\r\n";
    if(!it->second.Verification)
    {
        std::ostringstream os;
        std::string pwd;
        int pwd_length;
        Getline(buf,pwd,len,pwd_length);
        muduo::MutexLockGuard lock(map_conf_lock);
        if(pwd.compare(map_conf["monitor_pwd"] + "\r"))
        {
            LOG_INFO << "sessionID => "<<sessionId << "\tcan not be verify, RS_server will close the connection.";
            std::string reply = "password or needTpye => " + pwd + "\tcan not be verify, RS_server will close the connection.";
            BATNetSDKRaw_Send(MonitorHandler,sessionId,reply.c_str(),reply.length());
            BATNetSDKRaw_Disconnect(MonitorHandler,sessionId);
            map_count->decrement("monitor_max_conn");
        }
        else
        {
            LOG_INFO << "sessionID: "<<sessionId << "\tverify success.";
            it->second.Verification = true;
        }
        return pwd_length;
    }
    else
    {
        if(!it->second.Authority)
        {
            std::ostringstream os;
            std::string pwd;
            int pwd_length;
            Getline(buf,pwd,len,pwd_length);
            if(!pwd.compare("total_count\r"))
            {
                os << map_count->get("total_count") << "\n" << end_flag;
                BATNetSDKRaw_Send(MonitorHandler,sessionId,os.str().c_str(),os.str().length());
                os.str("");
            }
            else if(!pwd.compare("day_total_count\r"))
            {
                os << map_count->get("day_total_count") << "\n" << end_flag;
                BATNetSDKRaw_Send(MonitorHandler,sessionId,os.str().c_str(),os.str().length());
                os.str("");
            }
            else if(!pwd.compare("recv_speed\r"))
            {
                int before = map_count->get("day_total_count");
                sleep(1);
                Count_day_total_count();
                os << map_count->get("day_total_count") - before << "\n" << end_flag;
                BATNetSDKRaw_Send(MonitorHandler,sessionId,os.str().c_str(),os.str().length());
                os.str("");
            }
            else if(!pwd.compare("misunderstand_count\r"))
            {
                os << map_count->get("misunderstand_count") << "\n" << end_flag;
                BATNetSDKRaw_Send(MonitorHandler,sessionId,os.str().c_str(),os.str().length());
                os.str("");
            }
            else if(!pwd.compare("day_misunderstand_count\r"))
            {
                os << map_count->get("day_misunderstand_count") << "\n" << end_flag;
                BATNetSDKRaw_Send(MonitorHandler,sessionId,os.str().c_str(),os.str().length());
                os.str("");
            }
            else if(!pwd.compare("transmit_list_all\r"))
            {
                local_msg_queue.get_buff_info(os);
                BATNetSDKRaw_Send(MonitorHandler,sessionId,os.str().c_str(),os.str().length());
                os.str("");
            }
            else if(!pwd.compare("connection_timebuckets\r"))
            {
                muduo::MutexLockGuard lock(timebuckets_lock);
                timebuckets_Iter start = timebuckets.begin();
                while((start) != timebuckets.end())
                {
                    os << start->connect_establish.tm_year + 1900 << "-" << start->connect_establish.tm_mon + 1 << "-"<< start->connect_establish.tm_mday
                       << "-" << start->connect_establish.tm_hour + 8 << "-" << start->connect_establish.tm_min << "-" << start->connect_establish.tm_sec << "-----------";
                    if(start->closed)
                    {
                        os << start->connect_close.tm_year + 1900 << "-" << start->connect_close.tm_mon  + 1 << "-"<< start->connect_close.tm_mday
                           << "-" << start->connect_close.tm_hour + 8 << "-" << start->connect_close.tm_min << "-" << start->connect_close.tm_sec;
                    }
                    else
                    {
                        os << "Till Now";
                    }
                    os << "\n";
                    ++start;
                }
                os << end_flag;
                BATNetSDKRaw_Send(MonitorHandler,sessionId,os.str().c_str(),os.str().length());
                os.str("");
            }
            else if(!pwd.compare("server_starttime\r"))
            {
                os << RecvDataServerSatrtTime.tm_year + 1900 << "-" << RecvDataServerSatrtTime.tm_mon + 1 << "-" << RecvDataServerSatrtTime.tm_mday
                   << "\t\t" << RecvDataServerSatrtTime.tm_hour + 8 << ":" << RecvDataServerSatrtTime.tm_min << ":" << RecvDataServerSatrtTime.tm_sec << "\n" << end_flag;
                BATNetSDKRaw_Send(MonitorHandler,sessionId,os.str().c_str(),os.str().length());
                os.str("");
            }
            else if(!pwd.compare("queue_size\r"))
            {
                os << msg_queue.size() << "\n" << end_flag;
                BATNetSDKRaw_Send(MonitorHandler,sessionId,os.str().c_str(),os.str().length());
                os.str("");
            }
            else if(!pwd.compare("authentication_lists\r"))
            {
                typedef std::vector<std::string>::iterator list_Iter;
                os << "[admin_list]" << "\n";
                muduo::MutexLockGuard lock(list_map_lock);
                list_Iter admin_iter = list_map["admin"].begin();
                while (admin_iter != list_map["admin"].end())
                {
                    os << *admin_iter << "\n";
                    ++admin_iter;
                }
                os << "===============================================" << "\n" << "[all_list]" << "\n";
                list_Iter all_iter = list_map["all"].begin();
                while (all_iter != list_map["all"].end())
                {
                    os << *all_iter << "\n";
                    ++all_iter;
                }
                os << end_flag;
                BATNetSDKRaw_Send(MonitorHandler,sessionId,os.str().c_str(),os.str().length());
                os.str("");
            }
            else if(!pwd.compare("G_max_count\r"))
            {
                muduo::MutexLockGuard lock(map_conf_lock);
                os << "G_max_count = " << map_conf["G_max_count"]  << "\n" << end_flag;
                BATNetSDKRaw_Send(MonitorHandler,sessionId,os.str().c_str(),os.str().length());
                os.str("");
            }
            else if(!pwd.compare("admin\r"))
            {
                muduo::MutexLockGuard lock(list_map_lock);
                if(list_map["admin"].end() == std::find_if(list_map["admin"].begin(),list_map["admin"].end(),find_item<std::string,std::string,false>(it->first)))
                {
                    LOG_INFO << "client => " << it->first << "port => " << it->second.Port << " don't have privilge to do this!!";
                    os << "You don't have privilge to do this!!\n";
                    BATNetSDKRaw_Send(MonitorHandler,sessionId,os.str().c_str(),os.str().length());
                    os.str("");
                }
                else
                {
                    os << "[operation]:[object]:[value]\n";
                    BATNetSDKRaw_Send(MonitorHandler,sessionId,os.str().c_str(),os.str().length());
                    os.str("");
                    it->second.Authority = true;
                }
            }
            else if(!pwd.compare("exit()\r"))
            {
                os << std::string("Monitoring connection will be closed") << "\n";
                BATNetSDKRaw_Send(MonitorHandler,sessionId,os.str().c_str(),os.str().length());
                BATNetSDKRaw_Disconnect(MonitorHandler,sessionId);
                os.str("");
            }
            else
            {
                os << std::string("unrecognized command!!") << "\n" << end_flag;
                BATNetSDKRaw_Send(MonitorHandler,sessionId,os.str().c_str(),os.str().length());
                BATNetSDKRaw_Disconnect(MonitorHandler,sessionId);
                os.str("");
            }
            return pwd_length;
        }
        else
        {
            int cmd_length;
            std::ostringstream os;
            std::vector<std::string> cmd;
            GetCommand(buf,cmd,len,cmd_length);
            if(cmd.size() != 3)
            {
                std::string tmp("[operation]:[object]:[value]\n");
                BATNetSDKRaw_Send(MonitorHandler,sessionId,tmp.c_str(),tmp.length());
            }
            else if(!cmd[0].compare("set") && !cmd[1].compare("G_max_count"))
            {
                muduo::MutexLockGuard lock(map_conf_lock);
                map_conf["G_max_count"] = cmd[2];
                os << "true \n";
            }
            else if(!cmd[0].compare("reload_config"))
            {
                LoadConfig();
                LOG_INFO << "client => " << it->first << "\thas reload config!";
                os << "true \n";
            }
            else if(!cmd[0].compare("add"))
            {
                if(!cmd[1].compare("admin"))
                {
                    muduo::MutexLockGuard lock(list_map_lock);
                    list_map["admin"].push_back(cmd[2]);
                }
                else if(!cmd[1].compare("all"))
                {
                    muduo::MutexLockGuard lock(list_map_lock);
                    list_map["all"].push_back(cmd[2]);
                }
                os << "true \n";
                LOG_INFO << "client =>" << it->first << "\thas add new ip => " << cmd[2] << "\tto" << cmd[0] <<"\tlist";
            }
            else if(!cmd[0].compare("del"))
            {
                typedef std::vector<std::string>::iterator Iter;
                if(!cmd[1].compare("admin"))
                {
                    muduo::MutexLockGuard lock(list_map_lock);
                    Iter inter_it = std::find_if(list_map["admin"].begin(),list_map["admin"].end(),find_item<std::string,std::string,false>(cmd[2]));
                    if(inter_it == list_map["admin"].end())
                    {
                        os << cmd[0] << "\tlist has no ip => " << cmd[2] << "\tfailed \n";
                        LOG_INFO << "client = " << it->first << "\tdel ip => " << cmd[2] << "\tfrom" << cmd[0] <<"\tlist failed";
                    }
                    list_map["admin"].erase(inter_it);
                }
                else if(!cmd[1].compare("all"))
                {
                    muduo::MutexLockGuard lock(list_map_lock);
                    Iter inter_it = std::find_if(list_map["all"].begin(),list_map["all"].end(),find_item<std::string,std::string,false>(cmd[2]));
                    if(inter_it == list_map["all"].end())
                    {
                        os << cmd[0] << "\tlist has no ip => " << cmd[2] << "\tfailed  \n";
                        LOG_INFO << "client => " << it->first << "\tdel ip => " << cmd[2] << "\tfrom" << cmd[0] <<"\tlist failed";
                    }
                    list_map["all"].erase(inter_it);
                }
                os << "true \n";
                LOG_INFO << "client => " << it->first << "\thas del ip => " << cmd[2] << "\tfrom" << cmd[0] <<"\tlist";
            }
            else if(!cmd[0].compare("exit"))
            {
                os << std::string("exit admin mode") << "\n";
                os << "true \n";
                it->second.Authority = false;
            }
            os << end_flag;
            BATNetSDKRaw_Send(MonitorHandler,sessionId,os.str().c_str(),os.str().length());
            os.str("");
            return cmd_length;
        }
    }
    return 0;
}


int STBusAdapter::MonitorDataConnectCallback(int sessionId, int status, const char *ip, unsigned short port,
                                             void *userdata)
{
    LOG_INFO << "MonitorDataConnectCallback   sessionId => "<< sessionId<<"\tip => " << ip << "\tport => " << port <<"\tstatus => "<< status;
    if(!status)
    {
        int num;
        map_count->increment("monitor_max_conn");
        {
            muduo::MutexLockGuard lock(map_conf_lock);
            std::istringstream monitor_in(map_conf["monitor_max_conn"]);
            monitor_in >> num;
        }
        if(map_count->get("monitor_max_conn") > num)
        {
            LOG_INFO << "MonitorDataServer overload,has over monitor_max_conn : 10";
            BATNetSDKRaw_Disconnect(MonitorHandler,sessionId);
            map_count->decrement("monitor_max_conn");
        }

        if(map_monitor_connected.end() == std::find_if(map_monitor_connected.begin(),map_monitor_connected.end(),find_item<Map_Monitor_hasConnectrd_type,std::string,true>(std::string(ip))))
        {
            muduo::MutexLockGuard lock(Map_Monitor_hasConnectrd_lock);
            map_monitor_connected[ip] = {port,sessionId, false, false};
            LOG_INFO << "add clien => " << map_monitor_connected.begin()->first;
        }
    }
    else
    {
        LOG_INFO << "IP => " << ip << "\tPort => " << port << "\tDisconnected";
        map_count->decrement("monitor_max_conn");
        muduo::MutexLockGuard lock(Map_Monitor_hasConnectrd_lock);
        map_monitor_connected.erase(ip);
    }
}









