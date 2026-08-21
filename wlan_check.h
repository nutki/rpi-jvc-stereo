#ifndef WLAN_CHECK_H
#define WLAN_CHECK_H
int wlan_status(const char *ifname, int *signal, int *rxrate, int *txrate);
#endif
