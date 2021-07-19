#!/bin/bash
# SPDX-License-Identifier: BSD-3 Clause
# Copyright (C) 2020, Nokia

set -e

HERE=$(realpath $(dirname $0))

if [[ "$EUID" != 0 ]]; then
    echo "Running as un-privileged user, will use \`sudo\` where needed"
    SUDO="sudo"
else
    SUDO=""
fi

DATA_DIR=${DATA_DIR:-.}

# {c*}-[br0]-<delay-[br2]-aqm-[br1]-{s*}
HOST_PAIRS=${HOST_PAIRS:-1}
NETNS=(aqm delay)
for i in $(seq $HOST_PAIRS); do
     NETNS+=(c$i s$i)
 done
BRIDGES=(br0 br1 br2)

HOSTS_BACK="/etc/hosts.back"
function restore_hosts()
{
    [ -f "HOSTS_BACK" ] && $SUDO mv "$HOSTS_BACK" /etc/hosts || true
}

if [ -f "$HOSTS_BACK" ]; then
    restore_hosts
fi

declare -A ECN
declare -A ECNOPT
declare -A ECNUNSAFE
declare -A CCA
declare -A PAIR_DELAY
declare -A ACK_STRATEGY
declare -A ACK_STRATEGY_INITPERIODPACKETS
for i in $(seq $HOST_PAIRS); do
    ECN[s$i]=$(eval echo '${'CC${i}_ECN':-0}')
    ECN[c$i]=$(eval echo '${'CC${i}_ECN':-0}')
    ECNOPT[s$i]=$(eval echo '${'CC${i}_ECNOPT':-1}')
    ECNOPT[c$i]=$(eval echo '${'CC${i}_ECNOPT':-1}')
    ECNUNSAFE[s$i]=$(eval echo '${'CC${i}_ECNUNSAFE':-0}')
    ECNUNSAFE[c$i]=$(eval echo '${'CC${i}_ECNUNSAFE':-0}')
    CCA[c$i]=$(eval echo '${'CC${i}_CCA':-prague}')
    CCA[s$i]=$(eval echo '${'CC${i}_CCA':-prague}')
    PAIR_DELAY[$i]=$(eval echo '${'CC${i}_DELAY':-0ms}')
    ACK_STRATEGY[$i]=$(eval echo '${'CC${i}_ACK_STRATEGY':-immediate}')
    ACK_STRATEGY_INITPERIODPACKETS[$i]=$(eval echo '${'CC${i}_ACK_STRATEGY_INITPERIODPACKETS':-0}')
done

RATE=${RATE:-100Mbit}
DELAY=${DELAY:-0ms}
AQM=${AQM:-dualpi2}
LOG_PATTERN=${LOG_PATTERN:-}

TIME=${TIME:-10}

BASE_BR0=10.0.1
BASE_BR1=10.0.2
BASE_BR2=10.0.3
BASE_BR10=10.0

declare -A IPADDR
for i in $(seq $HOST_PAIRS); do
    IPADDR[c${i}-e0]="${BASE_BR0}.${i}"
    IPADDR[s${i}-e0]="${BASE_BR1}.${i}"
done
IPADDR[delay-e0]="${BASE_BR0}.254"
IPADDR[delay-e2]="${BASE_BR2}.253"
IPADDR[aqm-e2]="${BASE_BR2}.254"
IPADDR[aqm-e1]="${BASE_BR1}.254"

TC="tc"
IPERF="iperf3"
DELAY_DUMP=${HERE}/qdelay_dump
DELAY_DUMPER="${DELAY_DUMP}/qdelay_dump"
ACK_COALESCE="${HERE}/ack_coalescer"
ACK_COALESCER="${ACK_COALESCE}/coalescer"
ACK_FORWARDER="${ACK_COALESCE}/forwarder"
TCPDUMP=""

make -C "$DELAY_DUMP"
make -C "$ACK_COALESCE"

function macaddr()
{
    printf CA:FE:$(printf "$2" | od -t x1 -A n | awk '{ printf "%s:%s",$1,$2; }'):$(printf "$1" | od -t x1 -A n | awk '{ printf "%s:%s",$1,$2; }')
}

function _sudo()
{
    echo "# $@"
    $SUDO "$@"
}

function ns_exec_silent()
{
    $SUDO ip netns exec "$@"
}

function ns_exec()
{
    local nsname
    nsname="$1"
    shift 1
    echo "[${nsname}] $@"
    $SUDO ip netns exec "$nsname" "$@"
}

function ns_create_link()
{
    local ns=$1
    local name=$2
    local key="${ns}-${name}"
    local ipaddr="${IPADDR[$key]}"
    if [ ! -d "/sys/class/net/$key" ]; then
        _sudo ip link add "$key" type veth peer name "$name"
    fi
    if [ -d "/sys/class/net/$name" ]; then
        _sudo ip link set dev "$name" netns "$ns"
    fi
    ns_exec "$ns" ip link set dev "$name" up
    ns_exec "$ns" ethtool -K "$name" gro off gso off tso off
    ns_exec "$ns" ip link set dev "$name" address "$(macaddr $1 $2)"
    ns_exec "$ns" ip address add dev "$name" "${ipaddr}/24"
    _sudo ip link set dev "$key" up
    _sudo ethtool -K "$key" gro off gso off tso off

    echo "$ipaddr $ns"  | $SUDO tee -a /etc/hosts > /dev/null
}

function bridge_if()
{
    local brname="$1"
    local ifname="$2"
    _sudo ip link set dev "$brname" up
    _sudo ip link set master "$brname" dev "$ifname"
    _sudo ip link set dev "$ifname" up
}

function default_gw()
{
    ns_exec "$1" ip route add default via "${IPADDR[$2]}"
}

function gw_dev()
{
    ns_exec "$1" ip link set dev "$3" up
    ns_exec "$1" ip route add "$2" dev "$3"
}

function ack_coalescer()
{
    local ns=$1
    local clientid=$2
    local nsforward=$3

    local tunid="tun${clientid}"
    local key="c${clientid}-e0"
    local tgt="${IPADDR[$key]}"

    echo "[$ns] $ACK_COALESCER -i "$tunid" -s ${ACK_STRATEGY[$clientid]} -p ${ACK_STRATEGY_INITPERIODPACKETS[$clientid]}"
    ns_exec_silent "$ns" $ACK_COALESCER -i "$tunid" \
      -s "${ACK_STRATEGY[$clientid]}" \
      -p "${ACK_STRATEGY_INITPERIODPACKETS[$clientid]}" \
      > "${DATA_DIR}/coalescer_$(gen_suffix "c${clientid}").coal" 2>&1 &
    echo "[$ns] $ACK_FORWARDER -i $tunid"
    ns_exec_silent "$nsforward" $ACK_FORWARDER -i "$tunid" \
      > "${DATA_DIR}/coalescer_$(gen_suffix "c${clientid}").fwd" 2>&1 &

    sleep 0.1
    ns_exec "$ns" sysctl -qw "net.ipv6.conf.${tunid}.disable_ipv6=1"
    ns_exec "$nsforward" sysctl -qw "net.ipv6.conf.${tunid}.disable_ipv6=1"
    ns_exec "$nsforward" ip link set dev "${tunid}" up
    gw_dev "$ns" "$tgt" "${tunid}"
}

function setup_aqm()
{
    ns_exec aqm tc qdisc del dev e1 root &> /dev/null || true
    ns_exec aqm tc qdisc add dev e1 root handle 1: htb default 1 direct_qlen 10000
    ns_exec aqm tc class add dev e1 parent 1: classid 1:1 htb rate "$RATE" ceil "$RATE"
    ns_exec aqm "$TC" qdisc add dev e1 parent 1:1 handle 2: $AQM
    ns_exec delay tc qdisc del dev e0 root &> /dev/null || true
    ns_exec delay tc qdisc add dev e0 root handle 1: netem delay "$DELAY"

    for i in $(seq $HOST_PAIRS); do
        _sudo tc qdisc del root dev c$i-e0 &> /dev/null || true
        _sudo tc qdisc add dev c$i-e0 root handle 1: netem delay "${PAIR_DELAY[$i]}" 
    done
}

function setup()
{
    _sudo cp /etc/hosts "$HOSTS_BACK"

    for ns in "${NETNS[@]}"; do
        if [ ! -f "/var/run/netns/${ns}" ]; then
            _sudo ip netns add "$ns"
        fi
        ns_exec "$ns" ip link set dev lo up
    done

    for i in $(seq $HOST_PAIRS); do
        ns_create_link "c$i" e0
        ns_create_link "s$i" e0
    done

    ns_create_link delay e0
    ns_create_link delay e2
    ns_exec delay sysctl -qw net.ipv4.ip_forward=1
    ns_create_link aqm e2
    ns_create_link aqm e1
    ns_exec aqm sysctl -qw net.ipv4.ip_forward=1

    for br in "${BRIDGES[@]}"; do
        if [ ! -d "/sysclass/net/$br" ]; then
            _sudo ip link add dev "$br" type bridge
        fi
        _sudo ip link set dev "$br" up
    done

    bridge_if br0 delay-e0
    bridge_if br2 delay-e2
    bridge_if br2 aqm-e2
    bridge_if br1 aqm-e1
    default_gw delay aqm-e2
    default_gw aqm delay-e2

    for i in $(seq $HOST_PAIRS); do
        bridge_if br0 c${i}-e0
        bridge_if br1 s${i}-e0
        ack_coalescer aqm "$i" delay
        default_gw c${i} delay-e0
        default_gw s${i} aqm-e1
    done
    setup_aqm
}

function set_sysctl()
{
    ns_exec "$1" sysctl -qw "net.ipv4.tcp_congestion_control=${CCA[$1]}"
    ns_exec "$1" sysctl -qw "net.ipv4.tcp_ecn=${ECN[$1]}"
    ns_exec "$1" sysctl -qwe "net.ipv4.tcp_ecn_option=${ECNOPT[$1]}"
}

function gen_suffix()
{
    if [ -z "$LOG_PATTERN" ]; then
        printf "$1_${CCA[$1]}_${ECN[$1]}_${RATE}_${DELAY}_$(echo $AQM | tr ' /' '_')"
    else
        printf "$1_${LOG_PATTERN}"
    fi
}

function start_tcpdump()
{
    local ns=$1
    if [ -z "$TCPDUMP" ]; then
        return 0
    fi
    local dev="e0"
    if [ $# -gt 1 ]; then
        dev="$2"
    fi
    FILTER="ip and src net ${BASE_BR10}.0.0/16"
    ns_exec_silent "$ns" "$TCPDUMP" "-i" "$dev" "-s" "100" \
	"-w" "${DATA_DIR}/dump_${dev}_$(gen_suffix $ns).dump" \
	"$FILTER" &
}

function post_tcpdump()
{
    local ns=$1
    if [ -z "$TCPDUMP" ]; then
        return 0
    fi
    local dev="e0"
    if [ $# -gt 1 ]; then
        dev="$2"
    fi
    _sudo bzip2 -9 -f "${DATA_DIR}/dump_${dev}_$(gen_suffix $ns).dump"
}

function iperf_server()
{
    local ns=$1
    local suffix=$2
    shift 2
    FILTER="ip and src net ${BASE_BR0}.0/24"
    echo "[$ns] $IPERF -s -1 -i .05 -J $@ &> ${DATA_DIR}/iperf_$(gen_suffix $ns)${suffix}.json"
    ns_exec_silent "$ns" "$IPERF" -1 -s -i .1 -J "$@" \
        &> "${DATA_DIR}/iperf_$(gen_suffix $ns)${suffix}.json" &
    echo "[$ns] $DELAY_DUMPER e0 $FILTER > ${DATA_DIR}/qdelay_$(gen_suffix $ns).qdelay"
    ns_exec_silent "$ns" "$DELAY_DUMPER" "e0" "$FILTER" \
        > "${DATA_DIR}/qdelay_$(gen_suffix $ns).qdelay" &
}

function iperf_client()
{
    local ns=$1
    local dst=$2
    local suffix=$3
    shift 3
    echo "[$ns] $IPERF -c $dst -i .1 -t $TIME -J $@ &> ${DATA_DIR}/iperf_$(gen_suffix $ns)${suffix}.json"
    ns_exec_silent "$ns" "$IPERF" -c "$dst" -i .1 -t "$TIME" -J "$@" \
        &> "${DATA_DIR}/iperf_$(gen_suffix $ns)${suffix}.json"
}

function update_network()
{
    ns_exec delay tc qdisc change dev e0 root handle 1: netem delay "$DELAY"
    for i in $(seq $HOST_PAIRS); do
        _sudo tc qdisc change dev c$i-e0 root handle 1: netem delay "${PAIR_DELAY[$i]}" 
    done
}

function run_test()
{
    setup_aqm

    _sudo killall -w iperf &> /dev/null || true
    for i in $(seq $HOST_PAIRS); do
        set_sysctl s$i
        set_sysctl c$i
        start_tcpdump c$i
        start_tcpdump s$i
        iperf_server s$i ""
    done
    sleep .1

    echo "Running tests for ${TIME}sec"
    for i in $(seq $HOST_PAIRS); do
        iperf_client c$i s$i "" &
    done
    sleep $((TIME+5))
    $SUDO killall -w -SIGTERM $(basename "$IPERF") || true
    sleep 1
    $SUDO killall -w -SIGHUP $(basename "$DELAY_DUMPER")
    if [ ! -z "$TCPDUMP" ]; then
        $SUDO killall -w -SIGTERM $(basename "$TCPDUMP")
    fi
    sleep .1
    for i in $(seq $HOST_PAIRS); do
        post_tcpdump c$i
        post_tcpdump s$i
    done

    sync
}

function clean()
{
    for ns in "${NETNS[@]}"; do
        if [ ! -f "/var/run/netns/${ns}" ]; then
            continue
        fi
        _sudo ip link del dev "${ns}-e0" &> /dev/null || true
        _sudo ip link del dev "${ns}-e1" &> /dev/null || true
        _sudo ip netns pids "$ns" | xargs '-I{}' $SUDO kill '{}' &> /dev/null || true
        sleep 0.1
        _sudo ip netns pids "$ns" | xargs '-I{}' $SUDO kill -s9 '{}' &> /dev/null || true
	    _sudo ip netns del "$ns"
    done
    for br in "${BRIDGES[@]}"; do
        if [ ! -d "/sys/class/net/$br" ]; then
            continue
        fi
        _sudo ip link set dev "$br" down || true
        _sudo ip link del dev "$br" || true
    done
    _sudo rm -f ack_coal_pipe_tun*
    restore_hosts
}

trap clean SIGINT

function usage()
{
    echo "Usage: $THIS [-chpsvt]"
    echo "       -c   clean"
    echo "       -d   debug script execution"
    echo "       -h   show this message"
    echo "       -s   setup the network" 
    echo "       -u   update the network" 
    echo "       -t   run a test"
    echo ""
    echo "  Congestion control algorithms/ecn settings of nodes are controlled "
    echo "  through the following env variables:"
    echo "      HOST_PAIRS [$HOST_PAIRS] -- The number of host pairs"
    for i in $(seq $HOST_PAIRS); do
    echo "      CC${i}_CCA [${CCA[c$i]}] -- The net.ipv4.tcp_congestion_control sysctl for the c$i-s$i pairs"
    echo "      CC${i}_ECN [${ECN[c$i]}] -- The net.ipv4.tcp_ecn sysctl for the c$i-s$i pair "
    done
    echo ""
    echo "  Bottleneck link characteristics are controlled through the following env variables:"
    echo "      RATE [$RATE] -- The bottleneck bandwidth"
    echo "      DELAY [$DELAY] -- The base RTT"
    echo "      AQM [$AQM] -- The AQM parameters"
    echo ""
    echo "  Additional delay can be set for each sender-receiver pair:"
    for i in $(seq $HOST_PAIRS); do
    echo "      CC${i}_DELAY [${PAIR_DELAY[$i]}] -- The additional delay for the c$i-s$i pair"
    done
    echo ""
    echo "  Each transfert will run for TIME[$TIME] seconds."
    echo ""
    echo "  You can control the way the data is generated using:"
    echo "      DATA_DIR [$DATA_DIR] -- The directory where data should be stored"
    echo "      LOG_PATTERN [$LOG_PATTERN] -- The pattern to identify experiment data, instead of e.g., for c1, $(gen_suffix c1)"
}

DBG_FLAG=""
if [[ $# > 0 ]]; then
    while getopts "dchstu" o; do
        case "${o}" in
            c)
                clean
                ;;
            d)
                set -x
                ;;
            h)
                usage
                ;;
            s)
                setup
                ;;
            u)
                update_network
                ;;
            t)
                run_test
                ;;
            *)
                usage
                ;;
        esac
    done
else
    usage
fi
