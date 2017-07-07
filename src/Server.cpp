//
// Created by wangqi on 17-6-16.
//

#include "STBusAdapter.h"

int main()
{
    STBusAdapter adapter("../../src/list.xml",0,"");
    sleep(1);
    adapter.StartRecvDataServer();
    adapter.StartSendDataServer();
    adapter.StartMonitorServer();
    pause();
    return 0;
}