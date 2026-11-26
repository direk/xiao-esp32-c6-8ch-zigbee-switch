import assert from 'node:assert/strict';
import {test} from 'node:test';
import {execFileSync} from 'node:child_process';
import {mkdtempSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import definition from '../zigbee2mqtt/xiao-c6-8ch.mjs';

test('GET returns valid complete JSON for all 13 modes, rocker modes and eight channels', () => {
    const root = fileURLToPath(new URL('..', import.meta.url));
    const directory = mkdtempSync(join(tmpdir(), 'xiao-get-'));
    try {
        const executable = join(directory, 'get');
        execFileSync('cc', ['-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-I', 'firmware/main', 'firmware/main/control.c',
            'firmware/main/serial_input.c', 'tests/serial_get_harness.c', '-o', executable], {cwd: root});
        const rows = execFileSync(executable, {encoding: 'utf8'}).trim().split('\n').map((line) => JSON.parse(line));
        assert.equal(rows.length, 15 * 8);
        const modes = definition.exposes.find((x) => x.property === 'switch_type_ch1').values;
        const gestures = [
            ['single'], ['single', 'release'], ['single', 'double', 'triple', 'hold', 'release'],
            ['single', 'hold', 'release'], ['single', 'double', 'hold', 'release'],
            ['single', 'double', 'hold', 'release'], ['hold', 'release'],
            ['single', 'hold', 'release'], ['single', 'hold', 'release'], ['single'],
            ['hold', 'release'], ['double', 'hold', 'release'], ['double', 'hold', 'release'],
        ];
        for (let index = 0; index < rows.length; ++index) {
            const ep = index % 8;
            const rocker = index >= 13 * 8;
            const mode = Math.floor(index / 8) - (rocker ? 13 : 0);
            assert.deepEqual(rows[index], {
                channel: ep + 1, input_mode: rocker ? 'rocker' : 'push', switch_type: modes[mode],
                binding_priority: ep % 2 ? 'ON' : 'OFF', invert: Boolean(ep % 2),
                switch_actions: ['off', 'on', 'toggle'][ep % 3], min_level: 254, max_level: 254,
                transition_time: 600, color_temp_min: 1000, color_temp_max: 1000, scene_id: 255, group_id: 0xfff7,
                commands: rocker ? ['GET', 'PRESS', 'RELEASE', 'SET'] :
                    ['GET', 'PRESS', 'RELEASE', 'CLICK', 'DOUBLE', 'TRIPLE', 'HOLD'],
                gestures: rocker ? ['close', 'open'] : gestures[mode],
            });
        }
    } finally {
        rmSync(directory, {recursive: true, force: true});
    }
});


test('INFO and STATUS JSON preserve types, diagnostics, maximum counters and optional IDs', () => {
    const root = fileURLToPath(new URL('..', import.meta.url));
    const directory = mkdtempSync(join(tmpdir(), 'xiao-session-'));
    try {
        const executable = join(directory, 'session');
        execFileSync('cc', ['-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-I', 'firmware/main', 'firmware/main/control.c',
            'firmware/main/serial_input.c', 'firmware/main/serial_session.c',
            'tests/serial_session_test.c', '-o', executable], {cwd: root});
        const lines = execFileSync(executable, ['--json'], {encoding: 'utf8'}).trim().split('\n');
        assert.equal(lines.length, 3);
        const info = JSON.parse(lines[0]);
        assert.deepEqual(info, {firmware: '2.2.0', protocol: 2, model: 'XIAO-C6-8CH', boot_id: 4294967295,
            channels: 8, baud: 115200, format: '8N1', port: 'uart', transports: ['uart','usb'], logs_enabled: false, request_ids: true, id_order: 'increasing', dedup_depth: 16, hold_timeout_ms: 10000,
            commands: ['PING','INFO','STATUS','GET','PRESS','RELEASE','CLICK','DOUBLE','TRIPLE','HOLD','SET','RELEASE ALL','RESET','LOG'],
            antenna: 'internal', ota: {enabled: true, manufacturer: 0x131b, image_type: 0xc600, file_version: 0x02020000,
                slot: 'ota_1', state: 'downloading', received: 4294967295, total: 4294967295}});
        const offline = JSON.parse(lines[1]);
        assert.equal(offline.uptime_ms, 123456);
        assert.deepEqual(offline.zigbee, {joined: false, bindings_known: false, factory_new: true,
            reset_pending: false, short_address: null});
        assert.equal(offline.channels.length, 8);
        assert.ok(offline.channels.every((x) => x.binding_status === 'unknown'));
        assert.match(lines[2], /^@4294967295 \{/);
        const active = JSON.parse(lines[2].replace(/^@\d+ /, ''));
        assert.deepEqual(active.zigbee, {joined: true, bindings_known: true, factory_new: false,
            reset_pending: true, short_address: 0xabcd});
        for (let i = 0; i < 8; ++i) {
            assert.deepEqual(active.channels[i], {channel: i + 1, binding_status: 'bound', binding_clusters: 15,
                control_path: ['binding','z2m','mixed'][i % 3], physical: i === 0 || i === 7,
                virtual: true, sequence_active: true});
        }
        assert.ok(Object.values(active.counters).every((value) => value >= 4294967294 && value <= 4294967295));
        assert.ok(lines[2].length < 2048);
    } finally {
        rmSync(directory, {recursive: true, force: true});
    }
});
