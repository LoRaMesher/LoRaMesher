#!/usr/bin/env python3
"""
LoRaMesher Experiment Log Parser

Parses structured log output from ESP32 nodes running LoRaMesher and produces
CSV files and summary analysis. Designed for the 15-node experiment but works
with any number of nodes and future experiment runs.

Usage:
    python parse_logs.py [LOG_DIRECTORY] [--output OUTPUT_DIR]

Log format expected (PlatformIO serial monitor with timestamp filter):
    [YYYY-MM-DD HH:MM:SS.mmm] <ANSI color>[LEVEL] [0xADDR] message<ANSI reset>
"""

import argparse
import csv
import os
import re
import sys
from collections import defaultdict
from datetime import datetime
from pathlib import Path

import pandas as pd
import numpy as np

# ---------------------------------------------------------------------------
# ANSI escape stripper
# ---------------------------------------------------------------------------
ANSI_RE = re.compile(r'\x1b\[[0-9;]*m|\[(?:0|[0-9;]*)m')

def strip_ansi(s: str) -> str:
    return ANSI_RE.sub('', s)

# ---------------------------------------------------------------------------
# Timestamp parsing
# ---------------------------------------------------------------------------
TIMESTAMP_RE = re.compile(r'^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]\s*(.*)')

def parse_timestamp(ts_str: str) -> datetime:
    return datetime.strptime(ts_str, '%Y-%m-%d %H:%M:%S.%f')

# ---------------------------------------------------------------------------
# Node address extraction from filename
# ---------------------------------------------------------------------------
FILENAME_ADDR_RE = re.compile(r'-([0-9A-Fa-f]{4})\.log$')

def extract_node_from_filename(filename: str) -> str | None:
    m = FILENAME_ADDR_RE.search(filename)
    if m:
        return '0x' + m.group(1).upper()
    return None

# ---------------------------------------------------------------------------
# Log line patterns (applied AFTER stripping ANSI codes)
# ---------------------------------------------------------------------------

# PKT_RX src=0x006C dst=0xFFFF type=0x32 size=110 rssi=-128 snr=-7
PKT_RX_RE = re.compile(
    r'\[(?:INFO|DEBUG)\]\s+\[(0x[0-9A-Fa-f]+)\]\s+'
    r'PKT_RX\s+src=(0x[0-9A-Fa-f]+)\s+dst=(0x[0-9A-Fa-f]+)\s+'
    r'type=(0x[0-9A-Fa-f]+)\s+size=(\d+)\s+rssi=(-?\d+)\s+snr=(-?\d+)'
)

# PKT_TX dst=0xFFFF src=0x006C type=0x46 size=20
PKT_TX_RE = re.compile(
    r'\[(?:INFO|DEBUG)\]\s+\[(0x[0-9A-Fa-f]+)\]\s+'
    r'PKT_TX\s+dst=(0x[0-9A-Fa-f]+)\s+src=(0x[0-9A-Fa-f]+)\s+'
    r'type=(0x[0-9A-Fa-f]+)\s+size=(\d+)'
)

# RTENTRY dest=0x7984 via=0x006C hops=2 quality=128 active=1 nm=0
RTENTRY_RE = re.compile(
    r'\[(?:INFO|DEBUG)\]\s+\[(0x[0-9A-Fa-f]+)\]\s+'
    r'RTENTRY\s+dest=(0x[0-9A-Fa-f]+)\s+via=(0x[0-9A-Fa-f]+)\s+'
    r'hops=(\d+)\s+quality=(\d+)\s+active=(\d+)\s+nm=(\d+)'
)

# State transitions: "Network service state changed to 2" or "Protocol state changed to 2"
STATE_CHANGE_RE = re.compile(
    r'\[(?:INFO|DEBUG)\]\s+\[(0x[0-9A-Fa-f]+)\]\s+'
    r'(?:Network service |Protocol )?state changed to (\d+)'
)

# Transitioning from DISCOVERY to JOINING for network 0x6AEB
TRANSITION_RE = re.compile(
    r'\[(?:INFO|DEBUG)\]\s+\[(0x[0-9A-Fa-f]+)\]\s+'
    r'Transitioning from (\w+) to (\w+)'
)

# Slot transition: Slot 4 transition: type=SLEEP
SLOT_TRANSITION_RE = re.compile(
    r'\[(?:INFO|DEBUG)\]\s+\[(0x[0-9A-Fa-f]+)\]\s+'
    r'Slot (\d+) transition: type=(\w+)'
)

# Join request processing: *** PROCESSING JOIN_REQUEST from 0x7984 (state: 4, network_manager: 0x006C) ***
JOIN_REQUEST_RE = re.compile(
    r'\[(?:INFO|DEBUG)\]\s+\[(0x[0-9A-Fa-f]+)\]\s+'
    r'\*\*\* PROCESSING JOIN_REQUEST from (0x[0-9A-Fa-f]+)\s+\(state:\s*(\d+),\s*network_manager:\s*(0x[0-9A-Fa-f]+)\)'
)

# Join response: Join response from 0x006C: status=0, network=0x006C, slots=1
JOIN_RESPONSE_RE = re.compile(
    r'\[(?:INFO|DEBUG)\]\s+\[(0x[0-9A-Fa-f]+)\]\s+'
    r'Join response from (0x[0-9A-Fa-f]+):\s*status=(\d+),\s*network=(0x[0-9A-Fa-f]+),\s*slots=(\d+)'
)

# Join timeout: Join timeout - Fault recovery state
JOIN_TIMEOUT_RE = re.compile(
    r'\[(?:INFO|DEBUG)\]\s+\[(0x[0-9A-Fa-f]+)\]\s+'
    r'Join timeout'
)

# FAULT_RECOVERY timeout - restarting discovery
FAULT_RECOVERY_RE = re.compile(
    r'\[(?:WARNING|INFO|DEBUG)\]\s+\[(0x[0-9A-Fa-f]+)\]\s+'
    r'FAULT_RECOVERY timeout'
)

# Node accepted: Accepting node 0x7984 with 1 slots
JOIN_ACCEPT_RE = re.compile(
    r'\[(?:INFO|DEBUG)\]\s+\[(0x[0-9A-Fa-f]+)\]\s+'
    r'Accepting node (0x[0-9A-Fa-f]+) with (\d+) slots'
)

# Sync beacon received: Received sync beacon from 0x006C, hop count 0
SYNC_BEACON_RX_RE = re.compile(
    r'\[(?:INFO|DEBUG)\]\s+\[(0x[0-9A-Fa-f]+)\]\s+'
    r'Received sync beacon from (0x[0-9A-Fa-f]+),\s*hop count (\d+)'
)

# Source quality: Source 0xE464: remote_quality=0 ewma=252 link_quality=63 expected=3 received=721 [UNIDIRECTIONAL]
SOURCE_QUALITY_RE = re.compile(
    r'\[(?:INFO|DEBUG)\]\s+\[(0x[0-9A-Fa-f]+)\]\s+'
    r'Source (0x[0-9A-Fa-f]+):\s*remote_quality=(\d+)\s+ewma=(\d+)\s+'
    r'link_quality=(\d+)\s+expected=(\d+)\s+received=(\d+)'
    r'(?:\s+\[UNIDIRECTIONAL\])?'
)

# Remote link quality: Remote link quality from 0x77A4 for us (0x3428): 0
REMOTE_QUALITY_RE = re.compile(
    r'\[(?:INFO|DEBUG)\]\s+\[(0x[0-9A-Fa-f]+)\]\s+'
    r'Remote link quality from (0x[0-9A-Fa-f]+) for us \((0x[0-9A-Fa-f]+)\):\s*(\d+)'
)

# ---------------------------------------------------------------------------
# Message type name mapping
# ---------------------------------------------------------------------------
MSG_TYPE_NAMES = {
    '0x11': 'DATA',
    '0x32': 'ROUTING_TABLE',
    '0x42': 'JOIN_REQUEST',
    '0x43': 'JOIN_RESPONSE',
    '0x44': 'NM_CLAIM',
    '0x45': 'NM_ELECTION',
    '0x46': 'SYNC_BEACON',
}

STATE_NAMES = {
    '0': 'INITIALIZATION',
    '1': 'DISCOVERY',
    '2': 'JOINING',
    '3': 'NORMAL_OPERATION',
    '4': 'NETWORK_MANAGER',
    '5': 'FAULT_RECOVERY',
}

def msg_type_name(hex_type: str) -> str:
    return MSG_TYPE_NAMES.get(hex_type.upper().replace('0X', '0x'), hex_type)

def state_name(state_num: str) -> str:
    return STATE_NAMES.get(str(state_num), f'UNKNOWN({state_num})')

# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

class LogParser:
    def __init__(self, log_dir: str, output_dir: str):
        self.log_dir = Path(log_dir)
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)

        # Parsed data
        self.pkt_rx = []        # (node, timestamp, src, dst, msg_type, size, rssi, snr)
        self.pkt_tx = []        # (node, timestamp, dst, src, msg_type, size)
        self.rtentries = []     # (node, timestamp, dest, via, hops, quality, active, nm)
        self.state_changes = [] # (node, timestamp, new_state, source_type)
        self.transitions = []   # (node, timestamp, old_state, new_state)
        self.join_events = []   # (node, timestamp, event_type, details)
        self.slot_transitions = []  # (node, timestamp, slot_num, slot_type)
        self.source_quality = []    # (node, timestamp, source, remote_q, ewma, link_q, expected, received, unidirectional)
        self.remote_quality = []    # (node, timestamp, from_node, for_node, quality)

        # Reference time (earliest timestamp across all logs)
        self.ref_time: datetime | None = None
        self.nodes: set[str] = set()

    def _ms_since_ref(self, ts: datetime) -> int:
        if self.ref_time is None:
            return 0
        return int((ts - self.ref_time).total_seconds() * 1000)

    def _find_ref_time(self):
        """Scan all log files for the earliest timestamp."""
        earliest = None
        for logfile in sorted(self.log_dir.glob('*.log')):
            with open(logfile, 'r', errors='replace') as f:
                for line in f:
                    m = TIMESTAMP_RE.match(line.strip())
                    if m:
                        ts = parse_timestamp(m.group(1))
                        if earliest is None or ts < earliest:
                            earliest = ts
                        break  # Only need first timestamp per file
        self.ref_time = earliest

    def parse_all(self):
        self._find_ref_time()
        log_files = sorted(self.log_dir.glob('*.log'))
        if not log_files:
            print(f"No .log files found in {self.log_dir}")
            sys.exit(1)

        print(f"Found {len(log_files)} log files in {self.log_dir}")
        print(f"Reference time: {self.ref_time}")
        print()

        for logfile in log_files:
            self._parse_file(logfile)

        print(f"Nodes found: {sorted(self.nodes)}")
        print(f"  PKT_RX events:      {len(self.pkt_rx):>8,}")
        print(f"  PKT_TX events:      {len(self.pkt_tx):>8,}")
        print(f"  RTENTRY events:     {len(self.rtentries):>8,}")
        print(f"  State changes:      {len(self.state_changes):>8,}")
        print(f"  Transitions:        {len(self.transitions):>8,}")
        print(f"  Join events:        {len(self.join_events):>8,}")
        print(f"  Slot transitions:   {len(self.slot_transitions):>8,}")
        print(f"  Source quality:     {len(self.source_quality):>8,}")
        print(f"  Remote quality:     {len(self.remote_quality):>8,}")
        print()

    def _parse_file(self, filepath: Path):
        node_from_filename = extract_node_from_filename(filepath.name)
        if node_from_filename:
            self.nodes.add(node_from_filename)

        line_count = 0
        with open(filepath, 'r', errors='replace') as f:
            for raw_line in f:
                line_count += 1
                line = raw_line.strip()

                # Extract timestamp
                ts_match = TIMESTAMP_RE.match(line)
                if not ts_match:
                    continue
                ts = parse_timestamp(ts_match.group(1))
                rest = ts_match.group(2)

                # Strip ANSI codes
                rest = strip_ansi(rest)

                ms = self._ms_since_ref(ts)

                # --- PKT_RX ---
                m = PKT_RX_RE.search(rest)
                if m:
                    node, src, dst, msg_type, size, rssi, snr = m.groups()
                    self.nodes.add(node)
                    self.pkt_rx.append((
                        node, ms, ts.isoformat(), src, dst,
                        msg_type_name(msg_type), msg_type, int(size),
                        int(rssi), int(snr)
                    ))
                    continue

                # --- PKT_TX ---
                m = PKT_TX_RE.search(rest)
                if m:
                    node, dst, src, msg_type, size = m.groups()
                    self.nodes.add(node)
                    self.pkt_tx.append((
                        node, ms, ts.isoformat(), dst, src,
                        msg_type_name(msg_type), msg_type, int(size)
                    ))
                    continue

                # --- RTENTRY ---
                m = RTENTRY_RE.search(rest)
                if m:
                    node, dest, via, hops, quality, active, nm = m.groups()
                    self.nodes.add(node)
                    self.rtentries.append((
                        node, ms, ts.isoformat(), dest, via,
                        int(hops), int(quality), int(active), int(nm)
                    ))
                    continue

                # --- Transitioning from X to Y ---
                m = TRANSITION_RE.search(rest)
                if m:
                    node, old_state, new_state = m.groups()
                    self.nodes.add(node)
                    self.transitions.append((
                        node, ms, ts.isoformat(), old_state, new_state
                    ))
                    # Also record as state change
                    self.state_changes.append((
                        node, ms, ts.isoformat(), new_state, 'transition'
                    ))
                    continue

                # --- state changed to N ---
                m = STATE_CHANGE_RE.search(rest)
                if m:
                    node, new_state_num = m.groups()
                    self.nodes.add(node)
                    new_state = state_name(new_state_num)
                    # Determine type (protocol vs network service)
                    source = 'protocol' if 'Protocol state' in rest else 'network_service'
                    self.state_changes.append((
                        node, ms, ts.isoformat(), new_state, source
                    ))
                    continue

                # --- JOIN events ---
                m = JOIN_REQUEST_RE.search(rest)
                if m:
                    node, from_node, state_num, nm_addr = m.groups()
                    self.nodes.add(node)
                    self.join_events.append((
                        node, ms, ts.isoformat(), 'JOIN_REQUEST_PROCESSING',
                        f'from={from_node} state={state_name(state_num)} nm={nm_addr}'
                    ))
                    continue

                m = JOIN_RESPONSE_RE.search(rest)
                if m:
                    node, from_node, status, network, slots = m.groups()
                    self.nodes.add(node)
                    self.join_events.append((
                        node, ms, ts.isoformat(), 'JOIN_RESPONSE_RECEIVED',
                        f'from={from_node} status={status} network={network} slots={slots}'
                    ))
                    continue

                m = JOIN_ACCEPT_RE.search(rest)
                if m:
                    node, accepted_node, slots = m.groups()
                    self.nodes.add(node)
                    self.join_events.append((
                        node, ms, ts.isoformat(), 'JOIN_ACCEPT',
                        f'node={accepted_node} slots={slots}'
                    ))
                    continue

                m = JOIN_TIMEOUT_RE.search(rest)
                if m:
                    node = m.group(1)
                    self.nodes.add(node)
                    self.join_events.append((
                        node, ms, ts.isoformat(), 'JOIN_TIMEOUT', ''
                    ))
                    continue

                m = FAULT_RECOVERY_RE.search(rest)
                if m:
                    node = m.group(1)
                    self.nodes.add(node)
                    self.join_events.append((
                        node, ms, ts.isoformat(), 'FAULT_RECOVERY_RESTART', ''
                    ))
                    continue

                # --- Slot transition ---
                m = SLOT_TRANSITION_RE.search(rest)
                if m:
                    node, slot_num, slot_type = m.groups()
                    self.nodes.add(node)
                    self.slot_transitions.append((
                        node, ms, ts.isoformat(), int(slot_num), slot_type
                    ))
                    continue

                # --- Source quality stats ---
                m = SOURCE_QUALITY_RE.search(rest)
                if m:
                    node, source, rq, ewma, lq, exp, recv = m.groups()
                    self.nodes.add(node)
                    uni = 1 if '[UNIDIRECTIONAL]' in rest else 0
                    self.source_quality.append((
                        node, ms, ts.isoformat(), source,
                        int(rq), int(ewma), int(lq),
                        int(exp), int(recv), uni
                    ))
                    continue

                # --- Remote link quality ---
                m = REMOTE_QUALITY_RE.search(rest)
                if m:
                    node, from_node, for_node, quality = m.groups()
                    self.nodes.add(node)
                    self.remote_quality.append((
                        node, ms, ts.isoformat(), from_node,
                        for_node, int(quality)
                    ))
                    continue

        if node_from_filename:
            print(f"  Parsed {filepath.name}: {line_count:,} lines")

    # -------------------------------------------------------------------
    # CSV output
    # -------------------------------------------------------------------

    def write_csvs(self):
        self._write_pkt_rx_csv()
        self._write_pkt_tx_csv()
        self._write_rtentry_csv()
        self._write_state_transitions_csv()
        self._write_join_events_csv()
        self._write_slot_transitions_csv()
        self._write_source_quality_csv()
        self._write_remote_quality_csv()
        print(f"CSVs written to {self.output_dir}/")
        print()

    def _write_pkt_rx_csv(self):
        path = self.output_dir / 'pkt_rx.csv'
        with open(path, 'w', newline='') as f:
            w = csv.writer(f)
            w.writerow(['node', 'timestamp_ms', 'datetime', 'src', 'dst',
                         'msg_type', 'msg_type_hex', 'size', 'rssi', 'snr'])
            for row in sorted(self.pkt_rx, key=lambda r: r[1]):
                w.writerow(row)

    def _write_pkt_tx_csv(self):
        path = self.output_dir / 'pkt_tx.csv'
        with open(path, 'w', newline='') as f:
            w = csv.writer(f)
            w.writerow(['node', 'timestamp_ms', 'datetime', 'dst', 'src',
                         'msg_type', 'msg_type_hex', 'size'])
            for row in sorted(self.pkt_tx, key=lambda r: r[1]):
                w.writerow(row)

    def _write_rtentry_csv(self):
        path = self.output_dir / 'rtentry.csv'
        with open(path, 'w', newline='') as f:
            w = csv.writer(f)
            w.writerow(['node', 'timestamp_ms', 'datetime', 'dest', 'next_hop',
                         'hop_count', 'quality', 'active', 'is_nm'])
            for row in sorted(self.rtentries, key=lambda r: r[1]):
                w.writerow(row)

    def _write_state_transitions_csv(self):
        path = self.output_dir / 'state_transitions.csv'
        with open(path, 'w', newline='') as f:
            w = csv.writer(f)
            w.writerow(['node', 'timestamp_ms', 'datetime', 'new_state', 'source_type'])
            for row in sorted(self.state_changes, key=lambda r: r[1]):
                w.writerow(row)

    def _write_join_events_csv(self):
        path = self.output_dir / 'join_events.csv'
        with open(path, 'w', newline='') as f:
            w = csv.writer(f)
            w.writerow(['node', 'timestamp_ms', 'datetime', 'event_type', 'details'])
            for row in sorted(self.join_events, key=lambda r: r[1]):
                w.writerow(row)

    def _write_slot_transitions_csv(self):
        path = self.output_dir / 'slot_transitions.csv'
        with open(path, 'w', newline='') as f:
            w = csv.writer(f)
            w.writerow(['node', 'timestamp_ms', 'datetime', 'slot_num', 'slot_type'])
            for row in sorted(self.slot_transitions, key=lambda r: r[1]):
                w.writerow(row)

    def _write_source_quality_csv(self):
        path = self.output_dir / 'source_quality.csv'
        with open(path, 'w', newline='') as f:
            w = csv.writer(f)
            w.writerow(['node', 'timestamp_ms', 'datetime', 'source',
                         'remote_quality', 'ewma', 'link_quality',
                         'expected', 'received', 'unidirectional'])
            for row in sorted(self.source_quality, key=lambda r: r[1]):
                w.writerow(row)

    def _write_remote_quality_csv(self):
        path = self.output_dir / 'remote_quality.csv'
        with open(path, 'w', newline='') as f:
            w = csv.writer(f)
            w.writerow(['node', 'timestamp_ms', 'datetime', 'from_node',
                         'for_node', 'quality'])
            for row in sorted(self.remote_quality, key=lambda r: r[1]):
                w.writerow(row)

    # -------------------------------------------------------------------
    # Analysis
    # -------------------------------------------------------------------

    def run_analysis(self):
        self._rssi_snr_matrix()
        self._pdr_summary()
        self._network_formation_timeline()
        self._unidirectional_link_analysis()

    # --- (f) RSSI/SNR matrix ---
    def _rssi_snr_matrix(self):
        print("=" * 70)
        print("RSSI/SNR MATRIX (mean values, direct packets only)")
        print("=" * 70)

        if not self.pkt_rx:
            print("  No PKT_RX data available.\n")
            return

        df = pd.DataFrame(self.pkt_rx, columns=[
            'node', 'timestamp_ms', 'datetime', 'src', 'dst',
            'msg_type', 'msg_type_hex', 'size', 'rssi', 'snr'
        ])

        # Group by (src -> receiver node) to get link-level RSSI/SNR
        grouped = df.groupby(['src', 'node']).agg(
            rssi_mean=('rssi', 'mean'),
            rssi_std=('rssi', 'std'),
            snr_mean=('snr', 'mean'),
            snr_std=('snr', 'std'),
            count=('rssi', 'count')
        ).reset_index()

        nodes = sorted(self.nodes)

        # Build RSSI matrix
        rssi_matrix = pd.DataFrame(np.nan, index=nodes, columns=nodes)
        snr_matrix = pd.DataFrame(np.nan, index=nodes, columns=nodes)
        count_matrix = pd.DataFrame(0, index=nodes, columns=nodes)

        for _, row in grouped.iterrows():
            src, rx = row['src'], row['node']
            if src in nodes and rx in nodes:
                rssi_matrix.loc[src, rx] = round(row['rssi_mean'], 1)
                snr_matrix.loc[src, rx] = round(row['snr_mean'], 1)
                count_matrix.loc[src, rx] = int(row['count'])

        # Save
        rssi_matrix.to_csv(self.output_dir / 'rssi_matrix.csv')
        snr_matrix.to_csv(self.output_dir / 'snr_matrix.csv')
        count_matrix.to_csv(self.output_dir / 'pkt_count_matrix.csv')
        grouped.to_csv(self.output_dir / 'link_stats.csv', index=False)

        # Print summary (compact)
        print("\nMean RSSI (src=row, receiver=col):")
        print(rssi_matrix.to_string(na_rep='-'))
        print("\nMean SNR (src=row, receiver=col):")
        print(snr_matrix.to_string(na_rep='-'))
        print(f"\nDetailed link stats saved to {self.output_dir / 'link_stats.csv'}")

        # Highlight asymmetric links
        print("\nAsymmetric/unidirectional links (A->B exists but B->A missing or very different):")
        for i, a in enumerate(nodes):
            for j, b in enumerate(nodes):
                if i >= j:
                    continue
                a_to_b = count_matrix.loc[a, b]
                b_to_a = count_matrix.loc[b, a]
                if (a_to_b > 0 and b_to_a == 0) or (b_to_a > 0 and a_to_b == 0):
                    direction = f"{a}->{b}" if a_to_b > 0 else f"{b}->{a}"
                    count = a_to_b if a_to_b > 0 else b_to_a
                    rssi_val = rssi_matrix.loc[a, b] if a_to_b > 0 else rssi_matrix.loc[b, a]
                    print(f"  UNIDIRECTIONAL: {direction} ({count} pkts, RSSI={rssi_val})")
                elif a_to_b > 0 and b_to_a > 0:
                    rssi_diff = abs(
                        (rssi_matrix.loc[a, b] or 0) - (rssi_matrix.loc[b, a] or 0)
                    )
                    if rssi_diff > 20:
                        print(f"  ASYMMETRIC RSSI: {a}<->{b}: "
                              f"{a}->{b} RSSI={rssi_matrix.loc[a, b]} ({a_to_b} pkts), "
                              f"{b}->{a} RSSI={rssi_matrix.loc[b, a]} ({b_to_a} pkts), "
                              f"diff={rssi_diff:.1f} dB")
        print()

    # --- (g) PDR summary ---
    def _pdr_summary(self):
        print("=" * 70)
        print("PDR SUMMARY (Packet Delivery Ratio by message type)")
        print("=" * 70)

        if not self.pkt_tx or not self.pkt_rx:
            print("  Insufficient data.\n")
            return

        df_tx = pd.DataFrame(self.pkt_tx, columns=[
            'node', 'timestamp_ms', 'datetime', 'dst', 'src',
            'msg_type', 'msg_type_hex', 'size'
        ])
        df_rx = pd.DataFrame(self.pkt_rx, columns=[
            'node', 'timestamp_ms', 'datetime', 'src', 'dst',
            'msg_type', 'msg_type_hex', 'size', 'rssi', 'snr'
        ])

        # For broadcast packets (dst=0xFFFF), count how many distinct nodes received each TX
        broadcast_tx = df_tx[df_tx['dst'] == '0xFFFF'].copy()
        unicast_tx = df_tx[df_tx['dst'] != '0xFFFF'].copy()

        # Broadcast PDR: per sender per message type, how many receivers
        print("\nBroadcast TX counts (per sender per type):")
        if len(broadcast_tx) > 0:
            bc_summary = broadcast_tx.groupby(['src', 'msg_type']).size().reset_index(name='tx_count')
            # Count receptions of broadcasts from each source
            bc_rx = df_rx[df_rx['dst'] == '0xFFFF'].groupby(
                ['src', 'msg_type', 'node']
            ).size().reset_index(name='rx_count')
            bc_rx_agg = bc_rx.groupby(['src', 'msg_type']).agg(
                unique_receivers=('node', 'nunique'),
                total_receptions=('rx_count', 'sum')
            ).reset_index()

            bc_merged = bc_summary.merge(bc_rx_agg, on=['src', 'msg_type'], how='left').fillna(0)
            bc_merged['avg_receivers_per_tx'] = (
                bc_merged['total_receptions'] / bc_merged['tx_count']
            ).round(2)
            print(bc_merged.to_string(index=False))
            bc_merged.to_csv(self.output_dir / 'pdr_broadcast.csv', index=False)
        else:
            print("  No broadcast TX found.")

        # Unicast PDR: only count receptions at the intended destination node
        print("\nUnicast TX/RX summary (rx counted at intended destination only):")
        if len(unicast_tx) > 0:
            uc_summary = unicast_tx.groupby(['src', 'dst', 'msg_type']).size().reset_index(name='tx_count')
            # Only count packets received by the intended destination (node == dst)
            uc_rx_at_dest = df_rx[(df_rx['dst'] != '0xFFFF') & (df_rx['node'] == df_rx['dst'])].copy()
            uc_rx = uc_rx_at_dest.groupby(
                ['src', 'node', 'msg_type']
            ).size().reset_index(name='rx_count')
            uc_rx = uc_rx.rename(columns={'node': 'dst'})
            uc_merged = uc_summary.merge(uc_rx, on=['src', 'dst', 'msg_type'], how='left').fillna(0)
            uc_merged['rx_count'] = uc_merged['rx_count'].astype(int)
            uc_merged['pdr'] = (uc_merged['rx_count'] / uc_merged['tx_count'] * 100).round(1)
            print(uc_merged.to_string(index=False))
            uc_merged.to_csv(self.output_dir / 'pdr_unicast.csv', index=False)

            # Also show overheard unicast packets (nodes receiving packets not addressed to them)
            overheard = df_rx[(df_rx['dst'] != '0xFFFF') & (df_rx['node'] != df_rx['dst'])].copy()
            if len(overheard) > 0:
                oh_summary = overheard.groupby(['node', 'src', 'msg_type']).size().reset_index(name='count')
                print(f"\n  Overheard unicast packets (received by non-destination): {len(overheard)} total")
                oh_summary.to_csv(self.output_dir / 'overheard_unicast.csv', index=False)
        else:
            print("  No unicast TX found.")

        print()

    # --- (h) Network formation timeline ---
    def _network_formation_timeline(self):
        print("=" * 70)
        print("NETWORK FORMATION TIMELINE")
        print("=" * 70)

        # Find when each node first entered NORMAL_OPERATION (state 3) or NETWORK_MANAGER (state 4)
        operational_times = {}  # node -> (timestamp_ms, datetime_str, state)

        for node, ms, dt, new_state, source in self.state_changes:
            if new_state in ('NORMAL_OPERATION', 'NETWORK_MANAGER'):
                if node not in operational_times:
                    operational_times[node] = (ms, dt, new_state)

        # Detect NM from join_events (NM processes JOIN_REQUEST in state 4)
        # and from slot transitions (NM sends SYNC_BEACON_TX from start)
        for node, ms, dt, event_type, details in self.join_events:
            if event_type == 'JOIN_REQUEST_PROCESSING' and node not in operational_times:
                operational_times[node] = (ms, dt, 'NETWORK_MANAGER')
                break  # Only need the first one

        if not operational_times:
            print("  No nodes reached NORMAL_OPERATION.\n")
            return

        timeline = sorted(operational_times.items(), key=lambda x: x[1][0])

        print(f"\n{'Node':<10} {'Time (ms)':<12} {'Time (s)':<10} {'State':<22} {'DateTime'}")
        print("-" * 80)
        for node, (ms, dt, state) in timeline:
            print(f"{node:<10} {ms:<12} {ms/1000:<10.1f} {state:<22} {dt}")

        nodes_operational = set(operational_times.keys())
        nodes_never = self.nodes - nodes_operational
        if nodes_never:
            print(f"\nNodes that never reached NORMAL_OPERATION: {sorted(nodes_never)}")

        # Save
        rows = [(node, ms, dt, state) for node, (ms, dt, state) in timeline]
        with open(self.output_dir / 'formation_timeline.csv', 'w', newline='') as f:
            w = csv.writer(f)
            w.writerow(['node', 'timestamp_ms', 'datetime', 'state'])
            for row in rows:
                w.writerow(row)

        # Also count fault recovery events per node
        print("\nFault recovery events per node:")
        fault_counts = defaultdict(int)
        for node, ms, dt, event_type, details in self.join_events:
            if event_type in ('JOIN_TIMEOUT', 'FAULT_RECOVERY_RESTART'):
                fault_counts[node] += 1
        for node in sorted(self.nodes):
            count = fault_counts.get(node, 0)
            if count > 0:
                print(f"  {node}: {count} fault recovery events")
        if not fault_counts:
            print("  None")
        print()

    # --- (i) Unidirectional link analysis: 0x3428 <-> 0x77A4 ---
    def _unidirectional_link_analysis(self):
        print("=" * 70)
        print("UNIDIRECTIONAL LINK ANALYSIS: 0x3428 <-> 0x77A4")
        print("=" * 70)

        node_a = '0x3428'
        node_b = '0x77A4'

        df_rx = pd.DataFrame(self.pkt_rx, columns=[
            'node', 'timestamp_ms', 'datetime', 'src', 'dst',
            'msg_type', 'msg_type_hex', 'size', 'rssi', 'snr'
        ])
        df_rt = pd.DataFrame(self.rtentries, columns=[
            'node', 'timestamp_ms', 'datetime', 'dest', 'next_hop',
            'hop_count', 'quality', 'active', 'is_nm'
        ])

        # Packets from 0x77A4 received by 0x3428
        a_hears_b = df_rx[(df_rx['node'] == node_a) & (df_rx['src'] == node_b)]
        # Packets from 0x3428 received by 0x77A4
        b_hears_a = df_rx[(df_rx['node'] == node_b) & (df_rx['src'] == node_a)]

        print(f"\n{node_a} receives from {node_b}: {len(a_hears_b)} packets")
        if len(a_hears_b) > 0:
            # Note: RSSI values 126/127 are likely 8-bit signed overflow
            # (int8 wrapping of -130/-129). Flag them.
            suspicious_rssi = a_hears_b[a_hears_b['rssi'] > 0]
            print(f"  RSSI: mean={a_hears_b['rssi'].mean():.1f}, "
                  f"std={a_hears_b['rssi'].std():.1f}, "
                  f"min={a_hears_b['rssi'].min()}, max={a_hears_b['rssi'].max()}")
            if len(suspicious_rssi) > 0:
                print(f"  ** {len(suspicious_rssi)} packets have positive RSSI "
                      f"(likely int8 overflow, true value ~-129 to -130 dBm) **")
            print(f"  SNR:  mean={a_hears_b['snr'].mean():.1f}, "
                  f"std={a_hears_b['snr'].std():.1f}, "
                  f"min={a_hears_b['snr'].min()}, max={a_hears_b['snr'].max()}")
            print(f"  Message types: {dict(a_hears_b['msg_type'].value_counts())}")

        print(f"\n{node_b} receives from {node_a}: {len(b_hears_a)} packets")
        if len(b_hears_a) > 0:
            print(f"  RSSI: mean={b_hears_a['rssi'].mean():.1f}")
        else:
            print(f"  ** CONFIRMED UNIDIRECTIONAL: {node_b} never receives from {node_a} **")

        # Quality timeline: how 0x3428 sees 0x77A4 in its routing table
        rt_a_to_b = df_rt[(df_rt['node'] == node_a) & (df_rt['dest'] == node_b)].copy()
        rt_b_to_a = df_rt[(df_rt['node'] == node_b) & (df_rt['dest'] == node_a)].copy()

        print(f"\nRouting table quality timeline ({node_a}'s view of {node_b}):")
        if len(rt_a_to_b) > 0:
            print(f"  Entries: {len(rt_a_to_b)}")
            print(f"  Quality range: {rt_a_to_b['quality'].min()} - {rt_a_to_b['quality'].max()}")
            print(f"  Hop count values: {sorted(rt_a_to_b['hop_count'].unique())}")
            print(f"  Next hops used: {sorted(rt_a_to_b['next_hop'].unique())}")
            # Save timeline
            rt_a_to_b.to_csv(self.output_dir / 'quality_timeline_3428_to_77A4.csv', index=False)
        else:
            print("  No entries found.")

        print(f"\nRouting table quality timeline ({node_b}'s view of {node_a}):")
        if len(rt_b_to_a) > 0:
            print(f"  Entries: {len(rt_b_to_a)}")
            print(f"  Quality range: {rt_b_to_a['quality'].min()} - {rt_b_to_a['quality'].max()}")
            print(f"  Hop count values: {sorted(rt_b_to_a['hop_count'].unique())}")
            print(f"  Next hops used: {sorted(rt_b_to_a['next_hop'].unique())}")
            rt_b_to_a.to_csv(self.output_dir / 'quality_timeline_77A4_to_3428.csv', index=False)
        else:
            print("  No entries found.")

        # Show the quality evolution over time for the case study
        if len(rt_a_to_b) > 0:
            print(f"\n{node_a}'s route to {node_b} over time (first 30 entries):")
            print(f"  {'Time(ms)':<12} {'Via':<10} {'Hops':<6} {'Quality':<8} {'Active':<7}")
            print("  " + "-" * 50)
            for _, row in rt_a_to_b.head(30).iterrows():
                print(f"  {row['timestamp_ms']:<12} {row['next_hop']:<10} "
                      f"{row['hop_count']:<6} {row['quality']:<8} {row['active']:<7}")

        print()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='Parse LoRaMesher experiment logs and produce CSV analysis.'
    )
    parser.add_argument(
        'log_dir',
        nargs='?',
        default='.',
        help='Directory containing .log files (default: current directory)'
    )
    parser.add_argument(
        '--output', '-o',
        default=None,
        help='Output directory for CSVs (default: <log_dir>/output/)'
    )
    args = parser.parse_args()

    log_dir = os.path.abspath(args.log_dir)
    output_dir = args.output if args.output else os.path.join(log_dir, 'output')

    print(f"LoRaMesher Log Parser")
    print(f"Log directory: {log_dir}")
    print(f"Output directory: {output_dir}")
    print()

    lp = LogParser(log_dir, output_dir)
    lp.parse_all()
    lp.write_csvs()
    lp.run_analysis()

    print("Done.")


if __name__ == '__main__':
    main()
