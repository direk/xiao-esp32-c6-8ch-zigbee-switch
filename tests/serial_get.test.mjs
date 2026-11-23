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
