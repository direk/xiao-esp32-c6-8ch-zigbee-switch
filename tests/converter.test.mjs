import assert from 'node:assert/strict';
import {test} from 'node:test';
import {prepareDefinition} from 'zigbee-herdsman-converters';
import {Zcl} from 'zigbee-herdsman';
import source from '../zigbee2mqtt/xiao-c6-4ch.mjs';

const definition = prepareDefinition(source);
const fz = definition.fromZigbee.find((c) => c.cluster === 'xiao4Control');
// Mirror Z2M publish.ts dispatch: strip endpoint suffix before converter lookup,
// invoke each converter at most once per endpoint, then suffix optimistic state.
async function dispatch(message, meta, operation = 'convertSet') {
    const used = new Map();
    const published = {};
    for (const [property, value] of Object.entries(message)) {
        const match = /^(.*?)_(ch[1-4])$/.exec(property);
        const key = match ? match[1] : property;
        const endpointName = match?.[2];
        const ep = endpointName ? definition.endpoint(meta.device)[endpointName] : 1;
        const converter = definition.toZigbee.find((c) => c.key?.includes(key));
        assert.ok(converter, `Z2M cannot find converter for ${property} (normalized to ${key})`);
        const alreadyUsed = used.get(ep) ?? new Set();
        if (operation === 'convertSet' && alreadyUsed.has(converter)) continue;
        const entity = meta.device.getEndpoint(ep);
        const localMeta = {...meta, endpoint_name: endpointName};
        const result = operation === 'convertSet'
            ? await converter.convertSet(entity, key, value, localMeta)
            : await converter.convertGet(entity, key, localMeta);
        if (result?.state) {
            for (const [name, val] of Object.entries(result.state)) published[endpointName ? `${name}_${endpointName}` : name] = val;
        }
        alreadyUsed.add(converter);
        used.set(ep, alreadyUsed);
    }
    return Object.keys(published).length ? {state: published} : undefined;
}
const tz = {
    convertSet: (entity, key, value, meta) => dispatch({[key]: value}, meta),
    convertGet: (entity, key, meta) => dispatch({[key]: ''}, meta, 'convertGet'),
};

function fixture() {
    const calls = [];
    const customClusters = {};
    const endpoints = Array.from({length: 4}, (_, i) => ({
        ID: i + 1,
        write: async (cluster, values, options) => {
            const schema = Zcl.Utils.getCluster(cluster, undefined, customClusters);
            const payload = Object.entries(values).map(([name, attrData]) => {
                const attr = Zcl.Utils.getClusterAttribute(schema, name, undefined);
                assert.ok(attr);
                Zcl.Utils.processAttributeWrite(attr, attrData);
                return {attrId: attr.ID, dataType: attr.type, attrData};
            });
            // Exercise real ZCL encoding for every setting, not just a mocked API.
            const frame = Zcl.Frame.create(0, 0, false, undefined, 1, 'write', cluster, payload, customClusters);
            const bytes = frame.toBuffer();
            const parsed = Zcl.Frame.fromBuffer(0xfc00, Zcl.Header.fromBuffer(bytes), bytes, customClusters);
            calls.push({kind: 'write', ep: i + 1, values, options, parsed: parsed.payload});
        },
        read: async (cluster, attributes) => {
            const schema = Zcl.Utils.getCluster(cluster, undefined, customClusters);
            for (const name of attributes) assert.ok(Zcl.Utils.getClusterAttribute(schema, name, undefined));
            calls.push({kind: 'read', ep: i + 1, attributes});
        },
        bind: async () => assert.fail('Configure must not bind On/Off to coordinator'),
    }));
    const device = {
        endpoints, customClusters,
        getEndpoint: (id) => endpoints[id - 1],
        addCustomCluster: (name, schema) => { customClusters[name] = schema; calls.push({kind: 'cluster'}); },
        save: async () => { calls.push({kind: 'save'}); },
    };
    return {calls, device, meta: {device}};
}

test('real Z2M definition prepares and registers schema before configuration reads', async () => {
    const {device, calls} = fixture();
    await definition.configure(device, {}, definition);
    assert.equal(calls[0].kind, 'cluster');
    assert.deepEqual(calls.filter((x) => x.kind === 'read').map((x) => x.ep), [1, 2, 3, 4]);
    assert.deepEqual(definition.endpoint(device), {ch1: 1, ch2: 2, ch3: 3, ch4: 4});
    assert.equal(definition.exposes.find((x) => x.name === 'action').values.length, 12);
});

test('each setting reaches only its channel, with real Zigbee attribute encoding', async () => {
    const {device, calls, meta} = fixture();
    await definition.configure(device, {}, definition);
    for (let ep = 1; ep <= 4; ++ep) {
        const result = await tz.convertSet(null, `input_mode_ch${ep}`, 'rocker', meta);
        assert.deepEqual(result, {state: {[`input_mode_ch${ep}`]: 'rocker'}});
        assert.equal(calls.findLast((x) => x.kind === 'write').ep, ep);
        assert.deepEqual(calls.findLast((x) => x.kind === 'write').parsed, [{attrId: 1, dataType: 0x20, attrData: 1}]);
        await tz.convertSet(null, `invert_ch${ep}`, true, meta);
        assert.equal(calls.findLast((x) => x.kind === 'write').ep, ep);
        assert.deepEqual(calls.findLast((x) => x.kind === 'write').parsed, [{attrId: 2, dataType: 0x10, attrData: 1}]);
        await tz.convertGet(null, `input_mode_ch${ep}`, meta);
        assert.equal(calls.at(-1).ep, ep);
    }
});

test('only per-channel settings remain; removed heartbeat attributes are absent', async () => {
    const {device, meta} = fixture();
    await definition.configure(device, {}, definition);
    for (const key of ['heartbeat', 'fallback_timeout']) {
        assert.ok(!definition.toZigbee.some((c) => c.key?.includes(key)));
    }
    const schema = device.customClusters.xiao4Control;
    for (const name of ['heartbeat', 'timeout', 'challenge', 'route']) assert.equal(schema.attributes[name], undefined);
    for (const name of ['heartbeat', 'heartbeat_challenge', 'fallback_timeout', 'z2m_online']) {
        assert.ok(!definition.exposes.some((e) => e.name === name));
    }
    assert.equal(definition.fromZigbee.length, 1);
    await assert.rejects(tz.convertSet(null, 'input_mode_ch1', 'switch', meta));
    await assert.rejects(tz.convertSet(null, 'invert_ch1', 'true', meta));
});

test('Z2M dispatch preserves multiple settings on each channel and publishes no doubled suffix', async () => {
    const {device, calls, meta} = fixture();
    await definition.configure(device, {}, definition);
    const message = {input_mode_ch1: 'rocker', invert_ch1: true, input_mode_ch2: 'push', invert_ch2: false};
    const result = await dispatch(message, meta);
    assert.deepEqual(result, {state: {input_mode_ch1: 'rocker', invert_ch1: true, input_mode_ch2: 'push', invert_ch2: false}});
    const writes = calls.filter((x) => x.kind === 'write');
    assert.deepEqual(writes.map((x) => x.ep), [1, 1, 2, 2]);
    assert.deepEqual(writes.map((x) => x.parsed[0].attrId), [1, 2, 1, 2]);
    for (let i = 1; i <= 4; ++i) {
        const expose = definition.exposes.find((x) => x.property === `input_mode_ch${i}`);
        assert.equal(expose.name, 'input_mode');
        assert.equal(expose.endpoint, `ch${i}`);
    }
});

test('four channels and all commands decode independently, duplicate reports are suppressed', () => {
    for (let ep = 1; ep <= 4; ++ep) {
        const endpoint = {ID: ep};
        for (let cmd = 0; cmd < 3; ++cmd) {
            const event = ((cmd + 1) << 8) | cmd;
            const message = {endpoint, type: 'attributeReport', data: {event}};
            assert.deepEqual(fz.convert(definition, message), {action: `ch${ep}_${['off', 'on', 'toggle'][cmd]}`});
            assert.equal(fz.convert(definition, message), undefined);
        }
        // Out-of-order duplicate of the first event after two newer events.
        assert.equal(fz.convert(definition, {endpoint, type: 'attributeReport', data: {event: 256}}), undefined);
        // A new press with the same toggle command is a new action.
        assert.deepEqual(fz.convert(definition, {endpoint, type: 'attributeReport', data: {event: 1026}}), {action: `ch${ep}_toggle`});
    }
});

test('reads do not produce actions and legacy heartbeat reports have no effect', () => {
    const endpoint = {ID: 1};
    assert.deepEqual(fz.convert(definition, {
        endpoint, type: 'readResponse', data: {event: 258, inputMode: 1, inverted: 1, channelRoute: 0, bindingStatus: 2},
    }), {input_mode_ch1: 'rocker', invert_ch1: true, control_path_ch1: 'binding', binding_status_ch1: 'bound'});
    assert.equal(fz.convert(definition, {
        endpoint, type: 'attributeReport', data: {challenge: 42, route: 1, timeout: 20},
    }), undefined);
    assert.equal(fz.convert(definition, {endpoint: {ID: 5}, type: 'attributeReport', data: {event: 258}}), undefined);
    assert.equal(fz.convert(definition, {endpoint, type: 'attributeReport', data: {event: 259}}), undefined);
});

test('actual firmware-format attribute report parses with the registered custom schema', async () => {
    const {device} = fixture();
    await definition.configure(device, {}, definition);
    const bytes = Buffer.from([0x18, 0x7a, 0x0a, 0x12, 0x00, 0x20, 0x00]);
    const parsed = Zcl.Frame.fromBuffer(0xfc00, Zcl.Header.fromBuffer(bytes), bytes, device.customClusters);
    assert.equal(parsed.cluster.name, 'xiao4Control');
    const data = Object.fromEntries(parsed.payload.map((a) => [Zcl.Utils.getClusterAttribute(parsed.cluster, a.attrId, undefined).name, a.attrData]));
    assert.deepEqual(fz.convert(definition, {endpoint: {ID: 1}, type: 'attributeReport', data}), {control_path_ch1: 'binding'});
});

test('independent binding and route diagnostics decode on all four endpoints', () => {
    for (let ep = 1; ep <= 4; ++ep) {
        for (let status = 0; status <= 2; ++status) {
            for (let route = 0; route <= 1; ++route) {
                assert.deepEqual(fz.convert(definition, {
                    endpoint: {ID: ep}, type: 'attributeReport', data: {bindingStatus: status, channelRoute: route},
                }), {[`binding_status_ch${ep}`]: ['unknown', 'unbound', 'bound'][status],
                    [`control_path_ch${ep}`]: route === 0 ? 'binding' : 'z2m'});
            }
        }
    }
});
