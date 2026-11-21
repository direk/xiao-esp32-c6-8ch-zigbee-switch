// SPDX-License-Identifier: MIT
// Converter revision 1.2.0; firmware 1.2.0: binding when bound, actions otherwise.
import {presets as e, access as ea} from 'zigbee-herdsman-converters/lib/exposes';
import {deviceAddCustomCluster} from 'zigbee-herdsman-converters/lib/modernExtend';

const cluster = 'xiao4Control';
const commandNames = ['off', 'on', 'toggle'];
// Deduplicate event reports, not button commands: two real push presses have different counters.
const lastEvents = new WeakMap();
const customCluster = deviceAddCustomCluster(cluster, {
    name: cluster,
    ID: 0xfc00,
    attributes: {
        inputMode: {name: 'inputMode', ID: 0x0001, type: 0x20, write: true, min: 0, max: 1},
        inverted: {name: 'inverted', ID: 0x0002, type: 0x10, write: true},
        channelRoute: {name: 'channelRoute', ID: 0x0012, type: 0x20},
        bindingStatus: {name: 'bindingStatus', ID: 0x0013, type: 0x20},
        event: {name: 'event', ID: 0x0020, type: 0x23},
    },
    commands: {},
    commandsResponse: {},
});

// Z2M strips _chN before converter lookup, and passes the selected endpoint as entity.
// Return unsuffixed state too: Z2M adds _chN back when publishing the result.
// Separate converters prevent Z2M's once-per-converter rule from dropping a second
// setting for the same endpoint in a multi-setting MQTT /set message.
function setting(key, attr, encode) {
    return {
        key: [key],
        convertSet: async (entity, property, value, meta) => {
            const encoded = encode(value);
            const target = entity;
            if (!target || target.ID < 1 || target.ID > 4) throw new Error('Expected XIAO channel endpoint 1..4');
            await target.write(cluster, {[attr]: encoded}, {disableDefaultResponse: false});
            await target.read(cluster, [attr]);
            return {state: {[key]: value}};
        },
        convertGet: async (entity, property, meta) => {
            const target = entity;
            await target.read(cluster, [attr]);
        },
    };
}

const settings = [
    setting('input_mode', 'inputMode', (value) => {
        if (value !== 'push' && value !== 'rocker') throw new Error('input_mode must be push or rocker');
        return value === 'rocker' ? 1 : 0;
    }),
    setting('invert', 'inverted', (value) => {
        if (typeof value !== 'boolean') throw new Error('invert must be boolean');
        return value;
    }),
];

const received = {
    cluster,
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg) => {
        const ep = msg.endpoint.ID;
        if (ep < 1 || ep > 4) return;
        const data = msg.data;
        const out = {};
        if (data.inputMode !== undefined) out[`input_mode_ch${ep}`] = data.inputMode === 1 ? 'rocker' : 'push';
        if (data.inverted !== undefined) out[`invert_ch${ep}`] = Boolean(data.inverted);
        if (data.channelRoute !== undefined) out[`control_path_ch${ep}`] = data.channelRoute === 0 ? 'binding' : 'z2m';
        if (data.bindingStatus !== undefined) out[`binding_status_ch${ep}`] = ['unknown', 'unbound', 'bound'][data.bindingStatus] ?? 'unknown';
        // Reading configuration or the last event must never operate a light.
        if (msg.type === 'attributeReport' && data.event !== undefined) {
            const packed = data.event >>> 0;
            const command = packed & 0xff;
            const seen = lastEvents.get(msg.endpoint) ?? new Set();
            if (command <= 2 && packed !== 0 && !seen.has(packed)) {
                seen.add(packed);
                if (seen.size > 64) seen.delete(seen.values().next().value);
                lastEvents.set(msg.endpoint, seen);
                out.action = `ch${ep}_${commandNames[command]}`;
            }
        }
        return Object.keys(out).length ? out : undefined;
    },
};

export default {
    zigbeeModel: ['XIAO-C6-4CH-FAIL1'],
    model: 'XIAO-C6-4CH-FAIL1',
    vendor: 'DIY-XIAO',
    description: 'Four-channel switch: bound channels direct, unbound channels send actions',
    version: '0.0.3',
    extend: [customCluster, {
        isModernExtend: true,
        configure: [async (device) => {
            // Register the custom cluster before these reads. Keep On/Off binding targets user-controlled.
            for (let ep = 1; ep <= 4; ++ep) await device.getEndpoint(ep).read(cluster, ['inputMode', 'inverted', 'channelRoute', 'bindingStatus']);
            device.powerSource = 'Mains (single phase)';
            await device.save();
        }],
    }],
    endpoint: () => ({ch1: 1, ch2: 2, ch3: 3, ch4: 4}),
    meta: {multiEndpoint: true},
    fromZigbee: [received],
    toZigbee: settings,
    exposes: [
        e.action(Array.from({length: 4}, (_, i) => commandNames.map((c) => `ch${i + 1}_${c}`)).flat()),
        ...Array.from({length: 4}, (_, i) => [
            e.enum('input_mode', ea.ALL, ['push', 'rocker']).withEndpoint(`ch${i + 1}`).withCategory('config')
                .withDescription('push: toggle on press; rocker: closed=ON, open=OFF'),
            e.binary('invert', ea.ALL, true, false).withEndpoint(`ch${i + 1}`).withCategory('config')
                .withDescription('Invert the electrical input; changing configuration sends no light command'),
            e.enum('control_path', ea.STATE, ['z2m', 'binding']).withEndpoint(`ch${i + 1}`).withCategory('diagnostic'),
            e.enum('binding_status', ea.STATE, ['unknown', 'unbound', 'bound']).withEndpoint(`ch${i + 1}`).withCategory('diagnostic'),
        ]).flat(),
    ],
};
