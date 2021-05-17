#!/usr/bin/python3
# SPDX-License-Identifier: BSD-3 Clause
__copyright__ = 'Copyright (C) 2020, Nokia'

import argparse
import io
import itertools
import json
import os
import logging as log
import pathlib
import subprocess as sp
import sys
import time
import platform
import math

import matplotlib
import matplotlib.pyplot as plt
import numpy as np

matplotlib.use('Agg')
log.basicConfig(level=log.INFO)

plt.style.use('ggplot')

KERNEL_VERSION='.'.join(platform.release().split('.')[0:2])

ACCECN_ENABLED_VALUE=5
if float(KERNEL_VERSION) < 5.10:
    ACCECN_ENABLED_VALUE=3

US_PER_S = 1_000_000

font = {'family' : 'normal',
        'size'   : 14}

matplotlib.rc('font', **font)


def get_all_subclasses(cls):
    klasses = {}
    pending = cls.__subclasses__()
    while pending:
        k = pending.pop()
        if k.__name__ in klasses:
            continue
        pending.extend(k.__subclasses__())
        klasses[k.__name__] = k
    return klasses


def title_to_md_link(string):
    del_chars = set((')', '(', ':'))
    tr = {' ': '-'}
    return ''.join(tr.get(s, s).lower() for s in string if s not in del_chars)


class CCA(object):
    ALL = {}
    ECN = 0
    ECNOPT = 1
    ECNUNSAFE = 0
    EXTRA_RTT = 0

    @classmethod
    def discover_subclasses(cls):
        cls.ALL = get_all_subclasses(cls)

    @classmethod
    def get(cls, name):
        if not name:
            return None
        try:
            return cls.ALL[name]
        except KeyError as e:
            log.exception('Unknown congestion control %s', name)
            sys.exit()

    @classmethod
    def as_json(cls):
        return cls.__name__

def CCAFactory(name=None, color=None, aqm=None, ecn=None, ecnopt=-1, ecnunsafe=-1, params=None, extra_rtt=0, superklass=CCA):
    if superklass.__name__ == 'CCA':
        if name is None:
            raise ValueError('name required')
        if color is None:
            raise ValueError('color required')
        if aqm is None:
            raise ValueError('aqm required')
        if ecn is None:
            raise ValueError('ecn required')
        if ecnopt == -1:
            ecnopt = 1
        if ecnunsafe == -1:
            ecnunsafe = 0

    class NewCCA(superklass):
        @classmethod
        def set_params(cls):
            for k, w in cls.PARAMS.items():
                with open('/sys/module/tcp_%s/parameters/%s' % (cls.NAME, k), 'w') as f:
                    f.write(str(w))

        @classmethod
        def configure(cls):
            cls.set_params()
            return cls

        @classmethod
        def pretty_name(cls):
            if cls.PARAMS:
                return '%s (%s)' % (
                    cls.NAME,
                    ','.join(('%s=%s' % (k, v) for k, v in cls.PARAMS.items()))
                )
            return cls.NAME

        @classmethod
        def name(cls):
            return cls.__name__

        @classmethod
        def ecn_string(cls):
            if cls.ECN == 0:
                return ""
            if cls.ECN == 1:
                return "ECN"
            if cls.ECN == ACCECN_ENABLED_VALUE:
                s = "AccECN"
                if cls.ECNOPT == 0:
                    s += "-noopt"
                elif cls.ECNOPT == 2:
                    s += "-alwaysopt"
                elif cls.ECNOPT == 1:
                    pass
                else:
                    s = ''
                if s != '':
                    if cls.ECNUNSAFE:
                        s += '-unsafe'
                    return s

            return "ECN%sopt%s%s" % (cls.ECN, cls.ECNOPT, "-unsafe" if cls.ECNUNSAFE else "")

        @classmethod
        def aqm_name(cls):
            if cls.AQM == 'fq_codel_tst':
                return 'codel'
            return cls.AQM

        @classmethod
        def extra_rtt_string(cls):
            if cls.EXTRA_RTT == 0:
                return ''
            return str(cls.EXTRA_RTT).replace('.', 'd')

        @classmethod
        def params_string(cls):
            s = ''
            for k in sorted(cls.PARAMS.keys()):
                l = k
                if k.startswith(cls.NAME):
                    l = k[len(cls.NAME):].lstrip('-_')
                s += '-%s%s' % (l, cls.PARAMS[k])
            return s

    if name is not None:
        NewCCA.NAME = name.lower()
    if color is not None:
        NewCCA.COLOR = color
    if aqm is not None:
        NewCCA.AQM = aqm
        NewCCA.AQM_NAME = NewCCA.aqm_name()
    if ecn is not None:
        NewCCA.ECN = ecn
    if ecnopt != -1:
        NewCCA.ECNOPT = ecnopt
    if ecnunsafe != -1:
        NewCCA.ECNUNSAFE = ecnunsafe
    # E.g., for TCP Prague:
    # {
    #     'prague_rtt_scaling': '1',
    #     'prague_rtt_target': '25000',
    #     'prague_ecn_fallback': '0'
    # }
    NewCCA.PARAMS = params if params is not None else {}
    if extra_rtt != 0:
        NewCCA.EXTRA_RTT = extra_rtt

    NewCCA.__name__ = '%s%s%s%s' % (NewCCA.NAME.capitalize(),
                                    NewCCA.params_string(),
                                    NewCCA.extra_rtt_string(),
                                    NewCCA.ecn_string())

    return NewCCA

def avg_series(x, t, period):
    avg = np.empty(len(x))
    start = 0
    s = 0
    # Compute the rolling average over @period
    for i, (b, ts) in enumerate(zip(x, t)):
        while ts - t[start] > period:
            s -= x[start]
            start += 1
        s += b
        avg[i] = s / (i - start + 1)
    return avg


class Test(object):
    CWD = pathlib.Path(__file__).parent.absolute()
    SCRIPT = CWD / 'test_bottleneck.sh'
    DATA_DIR = CWD
    PLOT_SUBDIR = DATA_DIR / 'plots'
    BW_SCALE = 1_000_000  # Mb
    AQM = 'dualpi2'
    DURATION = 60  # s
    MTU = 1500
    _CFG = "test_cfg.json"

    @classmethod
    def set_DATA_DIR(cls, d):
        cls.DATA_DIR = d
        cls.PLOT_SUBDIR = d / 'plots'

    def __init__(self, cc1=None, cc2=[], bw=100, rtt=20, ack_strategy='immediate', env={}, title=''):
        if cc1 is None:
            raise ValueError('cc1 required')
        self.cc1 = cc1
        self.cc2 = cc2
        self.bw = bw
        self.rtt = rtt
        self.ack_strategy = ack_strategy
        self.env = self.build_env(env)
        self.title = title
        os.makedirs(self.PLOT_SUBDIR, exist_ok=True)

    @classmethod
    def cfg_file(cls):
        return cls.DATA_DIR / cls._CFG

    @classmethod
    def save_config(cls, testplan):
        with open(cls.cfg_file(), 'w') as f:
            json.dump(dict(data_dir=str(cls.DATA_DIR),
                           aqm=cls.AQM,
                           duration=cls.DURATION,
                           testplan=[t.as_json() for t in testplan]),
                      f)

    def as_json(self):
        return dict(test_type=self.__class__.__name__,
                    env=self.env, cc1=self.cc1.as_json(), title=self.title,
                    cc2=[cc.as_json() for cc in self.cc2], bw=self.bw, rtt=self.rtt,
                    ack_strategy=self.ack_strategy)

    def enumerate_cc2(self):
        for i, cc in enumerate(self.cc2):
            i += 2
            yield i, cc

    @property
    def cc2_names(self):
        return str(tuple(cc.pretty_name() for cc in self.cc2))

    @classmethod
    def args_from_json(cls, j):
        return dict(cc1=CCA.get(j['cc1']), cc2=[CCA.get(cc) for cc in j['cc2']],
                    bw=j.get('bw',100), rtt=j.get('rtt',20),
                    ack_strategy=j.get('ack_strategy', 'immediate'),
                    title=j.get('title',''), env=j['env'])

    @classmethod
    def from_json(cls, j):
        obj = cls(**cls.args_from_json(j))
        return obj

    @classmethod
    def load_config(cls):
        test_types = get_all_subclasses(cls)
        test_types[cls.__name__] = cls
        CCA.discover_subclasses()

        with open(cls.cfg_file(), 'r') as f:
            data = json.load(f)
        try:
            cls.AQM = data['aqm']
            cls.DURATION = data['duration']
            return [test_types[t['test_type']].from_json(t)
                    for t in data['testplan']]
        except KeyError as e:
            log.exception('Corrupted test config file')
            sys.exit()

    def configure(self):
        self.cc1.configure()
        for cc in self.cc2:
            cc.configure()

    def build_env(self, env):
        initperiodpackets = self.bw * self.rtt * 1000.0 / self.MTU * 4
        initperiodpackets = int(math.ceil(initperiodpackets))
        base = {
            'AQM': self.cc1.AQM,
            'RATE': '%dMbit' % self.bw,
            'DELAY': '%gms' % self.rtt,
            'ACK_STRATEGY': self.ack_strategy,
            'ACK_COALESCER_INITPERIODPACKETS': str(initperiodpackets),
            'CC1_CCA': self.cc1.NAME,
            'CC1_ECN': str(self.cc1.ECN),
            'CC1_ECNOPT': str(self.cc1.ECNOPT),
            'CC1_ECNUNSAFE': str(self.cc1.ECNUNSAFE),
            'CC1_DELAY': '%gms' % self.cc1.EXTRA_RTT,
            'TIME': '%d' % self.DURATION,
            'LOG_PATTERN': self.log_pattern,
            'DATA_DIR': str(self.DATA_DIR),
            'HOST_PAIRS': str(len(self.cc2) + 1)
        }
        base.update(env)
        for i, cc in self.enumerate_cc2():
            base['CC%d_CCA' % i] = cc.NAME
            base['CC%d_ECN' % i] = str(cc.ECN)
            base['CC%d_ECNOPT' % i] = str(cc.ECNOPT)
            base['CC%d_ECNUNSAFE' % i] = str(cc.ECNUNSAFE)
            base['CC%d_DELAY' % i] = '%gms' % cc.EXTRA_RTT
        return base

    def run_test(self):
        log.info('Running %s vs %s at %dMbit/%gms', self.cc1.pretty_name(),
                 self.cc2_names, self.bw, self.rtt)
        self.configure()
        self.env.update(os.environ)

        process = sp.Popen([self.SCRIPT, '-cst'], env=self.env)
        # Wait for iperf to stop/flush
        process.wait()

    @property
    def log_pattern(self):
        return '%s_%s_%s_%s_ack%s'% (self.cc1.__name__,
                            '_'.join(cc.__name__ for cc in self.cc2),
                            self.bw, self.rtt, self.ack_strategy)

    def process_qdelay_data(self, host):
        results = self.DATA_DIR / ('qdelay_%s_%s.qdelay' % (
            host, self.env['LOG_PATTERN']))
        try:
            with open(results, 'r') as input_data:
                data = json.load(input_data)
        except (IOError, json.JSONDecodeError) as e:
            log.exception("Could not load the qdelay results for client '%s'",
                          host)
            sys.exit()
        delay = np.empty(len(data['results']))
        t = np.empty(len(data['results']))
        ce = np.empty(len(data['results']))
        for i, d in enumerate(data['results']):
            delay[i] = d['delay-us'] / 1000.0
            t[i] = float(d['ts-us']) / US_PER_S
            ce[i] = d['ce']
        return t[0], delay, t, ce

    def process_bw_data(self, client, time_base, key, suffix='', scale=1):
        results = self.DATA_DIR / (
            'iperf_%s_%s%s.json' % (client, self.env['LOG_PATTERN'],
                                    '_%s' % suffix if suffix else ''))
        try:
            with open(results, 'r') as input_data:
                data = json.load(input_data)
        except (IOError, json.JSONDecodeError) as e:
            log.exception("Could not load the bandwidth results for client '%s'",
                          client)
            sys.exit()
        vals = np.empty(len(data['intervals']) + 1)
        t = np.empty(len(data['intervals']) + 1)
        vals[0] = 0
        t[0] = time_base
        for i, d in enumerate(data['intervals']):
            vals[i+1] = d['streams'][0][key] * scale
            t[i+1] = time_base + d['streams'][0]['end']
        return vals, t

    def plot_qdelay(self, ax):
        ax.set_ylabel('Queue delay [ms]')
        ax.set_ylim(0, 20)

        series = [('s1', self.cc1)]
        for i, cc in self.enumerate_cc2():
            series.append(('s%d' % i, cc))
        time_base = set()
        for host, cc in series:
            log.info('.. qdelay for client=%s', host)
            base, delay, t, ce = self.process_qdelay_data(host)
            time_base.add(base)
            ax.plot(t, delay, label=cc.pretty_name(), color=cc.COLOR, alpha=.9,
                    linewidth=.3) #, linestyle='dotted')
        ticks = sorted([0, 1, 5, 15])
        ax.set_yticks(ticks)
        ax.set_yticklabels(ticks)
        return min(time_base)

    def plot_ce(self, ax):
        ax.set_ylabel('CE prob')
        ax.set_ylim(0, 1)

        series = [('s1', self.cc1)]
        for i, cc in self.enumerate_cc2():
            series.append(('s%d' % i, cc))
        time_base = set()
        for host, cc in series:
            log.info('.. ce for client=%s', host)
            base, delay, t, ce = self.process_qdelay_data(host)
            time_base.add(base)
            avg = avg_series(ce, t, 0.2)
            ax.plot(t, avg, label=cc.pretty_name(), color=cc.COLOR, alpha=.9,
                    linewidth=.3) #, linestyle='dotted')
        return min(time_base)

    def legend(self, cc):
        return '%s%s@%dMbps/%gms/%s/ack%s' % (cc.pretty_name(),
                                              cc.ecn_string(),
                                              self.bw, self.rtt + cc.EXTRA_RTT,
                                              self.cc1.AQM_NAME,
                                              self.ack_strategy)

    def plot_bw(self, ax, time_base):
        ax.set_ylabel('Throughput [Mbps]')
        ax.set_ylim(-1, self.bw)
        ax.set_yticks([0, self.bw / 4, self.bw / 2,
                       self.bw * 3 / 4, self.bw])

        series = [(self.cc1, 'c1', self.legend(self.cc1))]
        for i, cc in self.enumerate_cc2():
            series.append((cc, 'c%d' % i, self.legend(cc)))
        for cc, data, name in series:
            log.info('.. bandwidth for client=%s', data)
            color = cc.COLOR
            bw, t = self.process_bw_data(data, time_base, 'bits_per_second', scale=1/self.BW_SCALE)
            avg = avg_series(bw, t, 1.0)
            ax.plot(t, avg, label=name,
                     color=color, alpha=.8, linewidth=1)
        ax.legend()

    def plot_cwnd(self, ax, time_base):
        ax.set_ylabel('CWND [bytes]')

        series = [(self.cc1, 'c1', self.legend(self.cc1))]
        for i, cc in self.enumerate_cc2():
            series.append((cc, 'c%d' % i, self.legend(cc)))
        for cc, data, name in series:
            log.info('.. CWND for client=%s', data)
            color = cc.COLOR
            cwnd, t = self.process_bw_data(data, time_base, 'snd_cwnd')
            ax.plot(t, cwnd, label=name,
                     color=color, alpha=.8, linewidth=1)
        ax.legend()

    def plot_rtt(self, ax, time_base):
        ax.set_ylabel('RTT')

        series = [(self.cc1, 'c1', self.legend(self.cc1))]
        for i, cc in self.enumerate_cc2():
            series.append((cc, 'c%d' % i, self.legend(cc)))
        for cc, data, name in series:
            log.info('.. CWND for client=%s', data)
            color = cc.COLOR
            rtt, t = self.process_bw_data(data, time_base, 'rtt', scale=0.001)
            ax.plot(t, rtt, label=name,
                     color=color, alpha=.8, linewidth=1)
        ax.legend()

    def plot(self):
        log.info('Plotting %s vs %s at %dMbit/%gms', self.cc1.pretty_name(),
                 self.cc2_names,
                 self.bw, self.rtt)

        fig, (ax0, ax1, ax2, ax3) = plt.subplots(
            nrows=4, figsize=(10, 12), sharex=True,
            gridspec_kw={ 'hspace': .1, 'height_ratios': [5, 5, 5, 5], })

        time_base = self.plot_qdelay(ax1)
        self.plot_bw(ax0, time_base)
        self.plot_ce(ax2)
#        self.plot_rtt(ax2, time_base)
        self.plot_cwnd(ax3, time_base)

        ax3.set_xlabel('Time [s]')
        ticks = [i * self.DURATION / 8 for i in range(9)]
        for ax in (ax0, ax1, ax2, ax3):
            ax.label_outer()
            ax.set_xlim(time_base, time_base + self.DURATION)
            ax.set_xticks([time_base + t for t in ticks])
        ax3.set_xticklabels([str(t) for t in ticks])
        if self.title:
            fig.suptitle(self.title)

        fig.savefig(self.fig_path('pdf'))
        fig.savefig(self.fig_path('png'), transparent=False, dpi=300)

        plt.close(fig)

    def fig_name(self, ext='pdf'):
        return '%s.%s' % (self.log_pattern, ext)

    def fig_path(self, ext='pdf'):
        return str(self.PLOT_SUBDIR / self.fig_name(ext))

    @classmethod
    def gen_report(cls, testplan):
        with open(cls.PLOT_SUBDIR / 'README.md', 'w') as f:
            f.write("""
# Description

See the [test config file]({cfg}).

""")

            headings = ["Test-%s: %s vs %s at %dMbit/%gms" % (
                i + 1, t.cc1.__name__, [cc.__name__ for cc in t.cc2],
                t.bw, t.rtt) for i, t in enumerate(testplan)]
            for h in headings:
                f.write(" * [{h}](#{link})\n".format(h=h,
                                                     link=title_to_md_link(h)))

            for t, h in zip(testplan, headings):
                f.write("""
# {h}

RTT: {rtt}ms

BW: {bw}Mbit

AQM: {aqm}

CCA for flow (1): {cc1}
""".format(rtt=t.rtt, bw=t.bw, aqm=t.cc1.AQM, h=h, cc1=t.cc1.pretty_name()))
                for i, cc in t.enumerate_cc2():
                    f.write('CCA for flow (%d): %s' % (i, cc.pretty_name()))
                f.write("""
![Result graph]({graph})

[Go back to index](#description)
""".format(graph=t.fig_name('png')))


def gen_testplan():
    testplan = []

    Prague = CCAFactory(name='Prague', color='blue', aqm='dualpi2', ecn=ACCECN_ENABLED_VALUE, ecnopt=1)
    DCTCP = CCAFactory(name='DCTCP', color='green', aqm='dualpi2', ecn=1, ecnopt=1)
    DCTCPAccECN = CCAFactory(name='DCTCP', color='green', aqm='dualpi2', ecn=ACCECN_ENABLED_VALUE, ecnopt=1)

    ccs = (Prague, DCTCP, DCTCPAccECN)

    for bw in [20]:
        for rtt in [40]:
            for strategy in ('immediate', 'halfdrop'):
                for cc in ccs:
                    testplan.append(Test(cc, bw=bw, rtt=rtt, ack_strategy=strategy))
    Test.save_config(testplan)
    return testplan


parser = argparse.ArgumentParser(
    description="Tests showing basic throughput/qdelay measurements",
    formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    epilog='If neither --execute-tests nor --plot are set, both of these steps will run'
)
parser.add_argument('-x', '--execute-tests', help='Execute the tests',
                    action='store_true', default=False)
parser.add_argument('-p', '--plot', help='Plot results', action='store_true',
                    default=False)
parser.add_argument('-o', '--open-plots', help='Open the plots',
                    action='store_true', default=False)
parser.add_argument('-c', '--cwd',
                    help='Set working directory to save/load results',
                    type=pathlib.Path, default=pathlib.Path('results'))
parser.add_argument('-I', '--ignore_modules',
                    help='Do not try to load required kernel modules',
                    action='store_true', default=False)
parser.add_argument('-g', '--generate-testplan',
                    help='Generate and save the testplan',
                    action='store_true', default=False)
args = parser.parse_args()

Test.set_DATA_DIR(args.cwd)
log.info("Test configured with DATA_DIR=%s", Test.DATA_DIR)

sp.check_call(['mkdir', '-p', str(Test.DATA_DIR)])

if args.generate_testplan:
    gen_testplan()

# if neither -x nor -p, defaults to all
if args.execute_tests or not args.plot:
    if not args.ignore_modules:
        try:
            sp.check_call(['sudo', 'modprobe', 'tcp_prague'])
            sp.check_call(['sudo', 'modprobe', 'tcp_dctcp'])
            sp.check_call(['sudo', 'modprobe', 'tcp_bbr2'])
            sp.check_call(['bash', '-c', 'cd %s && make' % str(Test.CWD / 'modules')])
            sp.check_call(['sudo', 'bash', '-c', 'cd %s && make load' % str(Test.CWD / 'modules')])
        except sp.CalledProcessError as e:
            log.exception('Failed to load the required kernel modules')
            sys.exit()
    else:
        log.info('Skipping kernel module check, this might prevent from running'
                 ' experiments!')

    testplan = gen_testplan()
    for t in testplan:
        t.run_test()

    try:
        sp.check_call([Test.SCRIPT, '-c'])
    except sp.CalledProcessError as e:
        log.exception('Failed to tear down the virtual network')
        sys.exit()
else:
    log.info('Skipping run_test()')

if args.plot or not args.execute_tests:
    testplan = Test.load_config()
    for t in testplan:
        t.plot()
    Test.gen_report(testplan)
else:
    log.info('Skipping plot()')

if args.open_plots:
    testplan = Test.load_config()
    for t in testplan:
        if (os.fork() == 0):
            sp.call(['xdg-open', t.fig_name()],
                    stdout=sp.DEVNULL, stderr=sp.DEVNULL)
