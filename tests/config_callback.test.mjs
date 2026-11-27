import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';

test('production config callbacks handle CH1 legacy-to-single without recursive SDK write hooks',()=>{
    const directory=mkdtempSync(join(tmpdir(),'xiao-config-hook-'));
    try {
        const main=readFileSync('firmware/main/main.c','utf8');
        const start=main.indexOf('static bool config_value('),end=main.indexOf('static void send_action(',start);
        assert.ok(start>0 && end>start);
        const ids=main.match(/^#define ATTR_.*$/gm).join('\n');
        const harness=readFileSync('tests/config_callback_harness.c','utf8')
            .replace('// MAIN_CONFIG_CALLBACKS',ids+'\n'+main.slice(start,end));
        const source=join(directory,'callback.c'),executable=join(directory,'callback');
        writeFileSync(source,harness);
        execFileSync('cc',['-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-I','firmware/main','firmware/main/control.c',
            'firmware/main/input_router.c','firmware/main/serial_input.c','firmware/main/config_write_guard.c',source,'-o',executable]);
        assert.match(execFileSync(executable,{encoding:'utf8',timeout:5000}),/^PASS:/);
    } finally {rmSync(directory,{recursive:true,force:true});}
});
