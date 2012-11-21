/********************************************************************************
 Copyright (C) 2012 Hugh Bailey <obs.jim@gmail.com>

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
********************************************************************************/


#include "Main.h"
#include "RTMPStuff.h"
#include "RTMPPublisher.h"

NetworkStream* CreateRTMPPublisher(String &failReason, bool &bCanRetry);


class DelayedPublisher : public NetworkStream
{
    DWORD delayTime;
    DWORD lastTimestamp;
    List<NetworkPacket> queuedPackets;

    RTMPPublisher *outputStream;

    bool bPublishingStarted;
    bool bConnecting, bConnected;
    bool bStreamEnding, bCancelEnd;

    static DWORD WINAPI CreateConnectionThread(DelayedPublisher *publisher)
    {
        String strFailReason;
        bool bRetry = false;

        publisher->outputStream = (RTMPPublisher*)CreateRTMPPublisher(strFailReason, bRetry);
        if(!publisher->outputStream)
        {
            App->SetStreamReport(strFailReason);

            if(!publisher->bStreamEnding)
                PostMessage(hwndMain, OBS_REQUESTSTOP, 1, 0);

            publisher->bCancelEnd = true;
        }
        else
        {
            publisher->bConnected = true;
            publisher->bConnecting = false;
        }

        return 0;
    }

    static INT_PTR CALLBACK EndDelayProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if(message == WM_INITDIALOG)
        {
            LocalizeWindow(hwnd);
            SetWindowLongPtr(hwnd, DWLP_USER, (LONG_PTR)lParam);
            return TRUE;
        }
        else if(message == WM_COMMAND && LOWORD(wParam) == IDCANCEL)
        {
            DelayedPublisher *publisher = (DelayedPublisher*)GetWindowLongPtr(hwnd, DWLP_USER);
        }
        return 0;
    }

    void ProcessPackets(DWORD timestamp)
    {
        if(bCancelEnd)
            return;

        if(timestamp > delayTime)
        {
            if(!bConnected)
            {
                if(!bConnecting)
                {
                    HANDLE hThread = OSCreateThread((XTHREAD)CreateConnectionThread, this);
                    OSCloseThread(hThread);

                    bConnecting = true;
                }
            }
            else
            {
                if(!bPublishingStarted)
                {
                    outputStream->BeginPublishing();
                    bPublishingStarted = true;
                }

                DWORD sendTime = timestamp-delayTime;
                for(UINT i=0; i<queuedPackets.Num(); i++)
                {
                    NetworkPacket &packet = queuedPackets[i];
                    if(packet.timestamp <= sendTime)
                    {
                        outputStream->SendPacket(packet.data.Array(), packet.data.Num(), packet.timestamp, packet.type);
                        packet.data.Clear();
                        queuedPackets.Remove(i--);
                    }
                }
            }
        }
    }

public:
    inline DelayedPublisher(DWORD delayTime)
    {
        this->delayTime = delayTime;
    }

    ~DelayedPublisher()
    {
        if(!outputStream || !outputStream->bStopping)
        {
            bStreamEnding = true;
            HWND hwndProgressDialog = CreateDialogParam(hinstMain, MAKEINTRESOURCE(IDD_ENDINGDELAY), hwndMain, (DLGPROC)EndDelayProc, (LPARAM)this);
            ShowWindow(hwndProgressDialog, TRUE);

            DWORD totalTimeLeft = delayTime;

            String strTimeLeftVal = Str("EndingDelay.TimeLeft");

            DWORD firstTime = OSGetTime();
            while(queuedPackets.Num() && !bCancelEnd)
            {
                DWORD timeElapsed = (OSGetTime()-firstTime);

                DWORD timeLeft = (totalTimeLeft-timeElapsed)/1000;
                DWORD timeLeftMinutes = timeLeft/60;
                DWORD timeLeftSeconds = timeLeft%60;

                String strTimeLeft = strTimeLeftVal;
                strTimeLeft.FindReplace(TEXT("$1"), FormattedString(TEXT("%u:%02u"), timeLeftMinutes, timeLeftSeconds));
                SetWindowText(GetDlgItem(hwndProgressDialog, IDC_TIMELEFT), strTimeLeft);

                ProcessPackets(lastTimestamp+timeElapsed);
                if(outputStream && outputStream->bStopping)
                    bCancelEnd = true;

                Sleep(10);
            }

            DestroyWindow(hwndProgressDialog);
        }

        for(UINT i=0; i<queuedPackets.Num(); i++)
            queuedPackets[i].data.Clear();

        delete outputStream;
    }

    void SendPacket(BYTE *data, UINT size, DWORD timestamp, PacketType type)
    {
        ProcessPackets(timestamp);

        NetworkPacket *newPacket = queuedPackets.CreateNew();
        newPacket->data.CopyArray(data, size);
        newPacket->timestamp = timestamp;
        newPacket->type = type;

        lastTimestamp = timestamp;
    }

    void BeginPublishing() {}

    double GetPacketStrain() const {return (outputStream) ? outputStream->GetPacketStrain() : 0;}
    QWORD GetCurrentSentBytes() {return (outputStream) ? outputStream->GetCurrentSentBytes() : 0;}
    DWORD NumDroppedFrames() const {return (outputStream) ? outputStream->NumDroppedFrames() : 0;}
};


NetworkStream* CreateDelayedPublisher(DWORD delayTime)
{
    return new DelayedPublisher(delayTime*1000);
}