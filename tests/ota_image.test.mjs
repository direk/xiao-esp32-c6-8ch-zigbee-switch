import {test} from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync, readFileSync, writeFileSync, rmSync, existsSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {parseOtaImage, getOtaFirmware} from 'zigbee-herdsman/dist/controller/helpers/ota.js';
import {prepareDefinition} from 'zigbee-herdsman-converters';
import source from '../zigbee2mqtt/xiao-c6-8ch.mjs';

test('converter enables OTA with the unchanged model identity with 24 endpoints', () => {
    const definition = prepareDefinition(source);
    assert.deepEqual(definition.ota, {modelId: 'XIAO-C6-8CH', manufacturerName: 'DIY-XIAO'});
    assert.equal(Object.keys(definition.endpoint()).length, 24);
});

test('independent herdsman decoder validates both generated antenna containers and the application payload', async () => {
    const directory = mkdtempSync(join(tmpdir(), 'xiao-ota-image-'));
    try {
        const app = Buffer.alloc(512, 0);
        app[0] = 0xe9; app.write('2.2.0', 48, 'ascii');
        const application = join(directory, 'application.bin');
        writeFileSync(application, app);
        const imageTypes = new Set();
        for (const antenna of ['internal', 'external']) {
            const output = join(directory, `${antenna}.zigbee`);
            execFileSync('python3', ['scripts/ota_image.py', '--application', application,
                '--version', '2.2.0', '--antenna', antenna, '--output', output]);
            const bytes = readFileSync(output);
            const image = parseOtaImage(bytes);
            assert.equal(image.header.manufacturerCode, 0x131b);
            assert.equal(image.header.imageType, antenna === 'internal' ? 0xc600 : 0xc601);
            assert.equal(image.header.fileVersion, 0x02020000);
            assert.equal(image.header.otaHeaderLength, 60);
            assert.equal(image.header.otaHeaderFieldControl, 4);
            assert.equal(image.header.minimumHardwareVersion, 1);
            assert.equal(image.header.maximumHardwareVersion, 1);
            assert.equal(image.header.totalImageSize, app.length + 66);
            assert.equal(image.elements.length, 1);
            assert.equal(image.elements[0].tagId, 0);
            assert.deepEqual(image.elements[0].data, app);
            const sha512 = createHash('sha512').update(bytes).digest('hex');
            assert.deepEqual(await getOtaFirmware(output, sha512), bytes);
            await assert.rejects(getOtaFirmware(output, '0'.repeat(128)));
            imageTypes.add(image.header.imageType);
        }
        assert.equal(imageTypes.size, 2, 'The antenna variants must never share an OTA imageType');
        assert.throws(() => execFileSync('python3', ['scripts/ota_image.py', '--application', application,
            '--version', '2.2.1', '--antenna', 'internal', '--output', join(directory, 'wrong.zigbee')], {stdio: 'pipe'}));
    } finally { rmSync(directory, {recursive: true, force: true}); }
});

test('published OTA index and both real firmware containers match their SHA512 and application images', {skip: !process.env.XIAO_OTA_DIST}, async () => {
    const directory = resolve(process.env.XIAO_OTA_DIST);
    const index = JSON.parse(readFileSync(join(directory, 'ota-index.json'), 'utf8'));
    assert.equal(index.length, 2);
    assert.equal(new Set(index.map((e) => e.imageType)).size, 2);
    for (const entry of index) {
        const bytes = await getOtaFirmware(join(directory, entry.fileName), entry.sha512);
        const image = parseOtaImage(bytes);
        assert.equal(image.header.manufacturerCode, entry.manufacturerCode);
        assert.equal(image.header.imageType, entry.imageType);
        assert.equal(image.header.fileVersion, entry.fileVersion);
        assert.equal(image.header.totalImageSize, entry.fileSize);
        const antenna = entry.imageType === 0xc600 ? 'internal' : 'external';
        assert.deepEqual(image.elements[0].data, readFileSync(join(directory, `application-${antenna}.bin`)));
        assert.deepEqual(entry.manufacturerName, ['DIY-XIAO']);
        assert.equal(entry.modelId, 'XIAO-C6-8CH');
        assert.ok(entry.url.endsWith(entry.fileName));
    }
});

test('tracked repository OTA index contains both images for the source release', {skip: !existsSync('ota/index.json')}, async () => {
    const version = /set\(PROJECT_VER "([^"]+)"\)/.exec(readFileSync('firmware/CMakeLists.txt', 'utf8'))[1];
    const [major, minor, patch] = version.split('.').map(Number);
    const expectedVersion = major * 0x1000000 + minor * 0x10000 + patch * 0x100;
    const index = JSON.parse(readFileSync('ota/index.json', 'utf8'));
    assert.equal(index.length, 2);
    assert.deepEqual(new Set(index.map((entry) => entry.imageType)), new Set([0xc600, 0xc601]));
    for (const entry of index) {
        const bytes = await getOtaFirmware(resolve('ota', entry.fileName), entry.sha512);
        const image = parseOtaImage(bytes);
        assert.equal(image.header.manufacturerCode, 0x131b);
        assert.equal(image.header.imageType, entry.imageType);
        assert.equal(image.header.fileVersion, expectedVersion);
        assert.equal(entry.fileVersion, expectedVersion);
        assert.equal(image.header.totalImageSize, entry.fileSize);
        assert.equal(image.elements[0].tagId, 0);
        assert.equal(image.elements[0].data[0], 0xe9);
        assert.equal(image.elements[0].data.subarray(48, 80).toString('ascii').split('\0')[0], version);
        assert.ok(entry.url.endsWith(`/v${version}/${entry.fileName}`));
    }
});
