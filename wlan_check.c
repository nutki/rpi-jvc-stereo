#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <net/if.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <linux/nl80211.h>
#include <linux/wireless.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

static int get_bssid(const char *ifname, unsigned char bssid[6]) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct iwreq wrq;
    memset(&wrq, 0, sizeof(wrq));
    strncpy(wrq.ifr_name, ifname, IFNAMSIZ - 1);

    if (ioctl(sock, SIOCGIWAP, &wrq) != 0) {
        perror("SIOCGIWAP");
        close(sock);
        return -1;
    }

    memcpy(bssid, wrq.u.ap_addr.sa_data, 6);
    close(sock);
    return 0;
}

static unsigned char bssid[6];
static int signal, rxrate, txrate;
static void print_rate_attr(struct nlattr *rate_info, int *dst) {
    struct nlattr *rate[NL80211_RATE_INFO_MAX + 1];
    nla_parse_nested(rate, NL80211_RATE_INFO_MAX, rate_info, NULL);
    if (rate[NL80211_RATE_INFO_BITRATE32]) {
        uint32_t v = nla_get_u32(rate[NL80211_RATE_INFO_BITRATE32]); /* 100 kbit/s */
        *dst = v;
        return;
    }
    if (rate[NL80211_RATE_INFO_BITRATE]) {
        uint16_t v = nla_get_u16(rate[NL80211_RATE_INFO_BITRATE]); /* 100 kbit/s */
        *dst = v;
        return;
    }
}

static int station_info_handler(struct nl_msg *msg, void *arg) {
    struct nlattr *attrs[NL80211_ATTR_MAX + 1];
    struct nlattr *sta_info[NL80211_STA_INFO_MAX + 1];
    struct genlmsghdr *ghdr = nlmsg_data(nlmsg_hdr(msg));
    nla_parse(attrs, NL80211_ATTR_MAX, genlmsg_attrdata(ghdr, 0),
              genlmsg_attrlen(ghdr, 0), NULL);

    if (!attrs[NL80211_ATTR_STA_INFO])
        return NL_SKIP;

    nla_parse_nested(sta_info, NL80211_STA_INFO_MAX,
                     attrs[NL80211_ATTR_STA_INFO], NULL);

    if (sta_info[NL80211_STA_INFO_TX_BITRATE]) {
        print_rate_attr(sta_info[NL80211_STA_INFO_TX_BITRATE], &txrate);
    }

    if (sta_info[NL80211_STA_INFO_RX_BITRATE]) {
        print_rate_attr(sta_info[NL80211_STA_INFO_RX_BITRATE], &rxrate);
    }
    if (sta_info[NL80211_STA_INFO_SIGNAL]) {
        signal = nla_get_s8(sta_info[NL80211_STA_INFO_SIGNAL]);
    }

    return NL_SKIP;
}

static int query_bitrates(const char *ifname) {
    struct nl_sock *sock = nl_socket_alloc();
    int family;
    int ifindex = if_nametoindex(ifname);
    struct nl_msg *msg;
    rxrate = txrate = signal = 0;

    if (!sock) return -1;
    if (genl_connect(sock) != 0) return -1;

    family = genl_ctrl_resolve(sock, "nl80211");
    if (family < 0) return -1;

    msg = nlmsg_alloc();
    if (!msg) return -1;

    genlmsg_put(msg, 0, 0, family, 0, 0,
                NL80211_CMD_GET_STATION, 0);
    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex);
    nla_put(msg, NL80211_ATTR_MAC, 6, bssid);

    nl_socket_modify_cb(sock, NL_CB_VALID, NL_CB_CUSTOM, station_info_handler, NULL);
    nl_send_auto_complete(sock, msg);
    nl_recvmsgs_default(sock);

    nlmsg_free(msg);
    nl_socket_free(sock);
    return 0;
}

int wlan_status(const char *ifname, int *psignal, int *prxrate, int *ptxrate) {
    if (!get_bssid(ifname, bssid) == 0) {
        return EXIT_FAILURE;
    }
    if (query_bitrates(ifname) != 0) {
        return EXIT_FAILURE;
    }
    *psignal = signal;
    *prxrate = rxrate;
    *ptxrate = txrate;
    return EXIT_SUCCESS;
}
