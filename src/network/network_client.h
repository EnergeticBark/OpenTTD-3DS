/* $Id$ */

/** @file network_client.h Client part of the network protocol. */

#ifndef NETWORK_CLIENT_H
#define NETWORK_CLIENT_H

#ifdef ENABLE_NETWORK

DEF_CLIENT_SEND_COMMAND(PACKET_CLIENT_GAME_INFO);
DEF_CLIENT_SEND_COMMAND(PACKET_CLIENT_COMPANY_INFO);
DEF_CLIENT_SEND_COMMAND_PARAM(PACKET_CLIENT_COMMAND)(const CommandPacket *cp);
DEF_CLIENT_SEND_COMMAND_PARAM(PACKET_CLIENT_ERROR)(NetworkErrorCode errorno);
DEF_CLIENT_SEND_COMMAND_PARAM(PACKET_CLIENT_QUIT)();
DEF_CLIENT_SEND_COMMAND_PARAM(PACKET_CLIENT_CHAT)(NetworkAction action, DestType desttype, int dest, const char *msg, int64 data);
DEF_CLIENT_SEND_COMMAND_PARAM(PACKET_CLIENT_PASSWORD)(NetworkPasswordType type, const char *password);
DEF_CLIENT_SEND_COMMAND_PARAM(PACKET_CLIENT_SET_PASSWORD)(const char *password);
DEF_CLIENT_SEND_COMMAND_PARAM(PACKET_CLIENT_SET_NAME)(const char *name);
DEF_CLIENT_SEND_COMMAND(PACKET_CLIENT_ACK);
DEF_CLIENT_SEND_COMMAND_PARAM(PACKET_CLIENT_RCON)(const char *pass, const char *command);
DEF_CLIENT_SEND_COMMAND_PARAM(PACKET_CLIENT_MOVE)(CompanyID company, const char *pass);

NetworkRecvStatus NetworkClient_ReadPackets(NetworkClientSocket *cs);
void NetworkClient_Connected();

#endif /* ENABLE_NETWORK */

#endif /* NETWORK_CLIENT_H */
