// SPDX-License-Identifier: MIT
// Compatible with firmware 2.x. Replace the previous converter; do not load both files.
import {presets as e, access as ea} from 'zigbee-herdsman-converters/lib/exposes';
import {deviceAddCustomCluster} from 'zigbee-herdsman-converters/lib/modernExtend';

const cluster = 'xiaoControl';
const channels = Array.from({length: 8}, (_, i) => `ch${i + 1}`);
const modes = ['toggle', 'momentary', 'multifunction', 'brightness_level', 'brightness_level_up',
    'brightness_level_down', 'move_to_color_temperature', 'move_to_color_temperature_up',
    'move_to_color_temperature_down', 'scene', 'brightness_level_no_onoff',
    'brightness_level_up_no_onoff', 'brightness_level_down_no_onoff'];
const actions = ['off', 'on', 'toggle', 'single', 'double', 'triple', 'hold', 'release',
    'brightness_move_up', 'brightness_move_down', 'brightness_step_up', 'brightness_step_down',
    'brightness_stop', 'color_temperature_move_up', 'color_temperature_move_down',
    'color_temperature_step_up', 'color_temperature_step_down', 'color_temperature_stop', 'scene'];
const fields = [
    ['input_mode', 'inputMode', 1, 0x20, ['push', 'rocker']],
    ['invert', 'inverted', 2, 0x10, null],
    ['switch_type', 'switchType', 5, 0x20, modes],
    ['switch_actions', 'switchAction', 6, 0x20, ['off', 'on', 'toggle']],
    ['min_level', 'minLevel', 7, 0x20, [1, 254]],
    ['max_level', 'maxLevel', 8, 0x20, [1, 254]],
    ['transition_time', 'transitionTime', 9, 0x21, [0, 600]],
    ['color_temp_min', 'colorMin', 10, 0x21, [1, 1000]],
    ['color_temp_max', 'colorMax', 11, 0x21, [1, 1000]],
    ['scene_id', 'sceneId', 12, 0x20, [0, 255]],
    ['group_id', 'sceneGroup', 13, 0x21, [0, 0xfff7]],
    ['binding_priority', 'bindingPriority', 14, 0x10, ['OFF', 'ON']],
];
const customCluster = deviceAddCustomCluster(cluster, {
    name: cluster, ID: 0xfc00,
    attributes: Object.fromEntries([
        ...fields.map(([, name, ID, type]) => [name, {name, ID, type, write: true}]),
        ['channelRoute', {name: 'channelRoute', ID: 0x12, type: 0x20}],
        ['bindingStatus', {name: 'bindingStatus', ID: 0x13, type: 0x20}],
        ['bindingClusters', {name: 'bindingClusters', ID: 0x14, type: 0x20}],
        ['event', {name: 'event', ID: 0x20, type: 0x23}],
    ]), commands: {}, commandsResponse: {},
});
const settings = fields.map(([key, attr, , type, values]) => ({
    key: [key],
    convertSet: async (entity, property, value) => {
        if (!entity || entity.ID < 1 || entity.ID > 8) throw new Error('Expected channel endpoint 1..8');
        let encoded;
        if (key === 'invert') {
            if (typeof value !== 'boolean') throw new Error('invert must be boolean');
            encoded = value;
        } else if (type === 0x10) {
            if (!values.includes(value)) throw new Error(`${key} must be ON or OFF`);
            encoded = value === 'ON';
        } else if (typeof values[0] === 'string') {
            encoded = values.indexOf(value);
            if (encoded < 0) throw new Error(`Invalid ${key}: ${value}`);
        } else {
            if (!Number.isInteger(value) || value < values[0] || value > values[1]) throw new Error(`${key} outside ${values[0]}..${values[1]}`);
            encoded = value;
        }
        await entity.write(cluster, {[attr]: encoded}, {disableDefaultResponse: false});
        await entity.read(cluster, [attr]);
        return {state: {[key]: value}};
    },
    convertGet: async (entity) => { await entity.read(cluster, [attr]); },
}));
const seenEvents = new WeakMap();
const received = {
    cluster, type: ['attributeReport', 'readResponse'],
    convert: (model, msg) => {
        const ep = msg.endpoint.ID;
        if (ep < 1 || ep > 8) return;
        const out = {}, data = msg.data;
        for (const [key, attr, , , values] of fields) {
            if (data[attr] === undefined) continue;
            out[`${key}_ch${ep}`] = key === 'invert' ? Boolean(data[attr]) :
                key === 'binding_priority' ? (data[attr] ? 'ON' : 'OFF') :
                typeof values[0] === 'string' ? values[data[attr]] : data[attr];
        }
        if (data.channelRoute !== undefined) out[`control_path_ch${ep}`] = ['binding', 'z2m', 'mixed'][data.channelRoute];
        if (data.bindingStatus !== undefined) out[`binding_status_ch${ep}`] = ['unknown', 'unbound', 'bound'][data.bindingStatus];
        if (data.bindingClusters !== undefined) out[`binding_clusters_ch${ep}`] = data.bindingClusters;
        if (msg.type === 'attributeReport' && data.event !== undefined) {
            const packed = data.event >>> 0, action = packed & 255;
            const seen = seenEvents.get(msg.endpoint) ?? new Set();
            if (packed && actions[action] && !seen.has(packed)) {
                seen.add(packed);
                if (seen.size > 64) seen.delete(seen.values().next().value);
                seenEvents.set(msg.endpoint, seen);
                out.action = `ch${ep}_${actions[action]}`;
            }
        }
        return Object.keys(out).length ? out : undefined;
    },
};
const descriptions = {
    input_mode: 'Physical input. rocker+toggle sends Toggle on both edges; rocker+momentary follows closed=ON/open=OFF. Advanced modes require push.',
    switch_type: 'Per-channel logical function. Brightness-only modes never send On/Off.',
    switch_actions: 'Push only: command on press in toggle/momentary mode. Rocker uses switch_type: toggle on both edges, momentary follows contact.',
    transition_time: 'Transition duration in 0.1 seconds; default 25 = 2.5 seconds.',
    binding_priority: 'Default ON: direct binding suppresses action. OFF: direct control also sends action. Saved on device.',
    group_id: 'Direct Zigbee scene group; 0 uses Scenes bindings instead.',
};
export default {
    zigbeeModel: ['XIAO-C6-8CH', 'XIAO-C6-4CH-FAIL1'],
    model: 'XIAO-C6-8CH', vendor: 'DIY-XIAO',
    description: 'Eight-channel mains-powered Zigbee switch/dimmer/scene controller, external antenna',
    version: '0.0.6',
    extend: [customCluster, {isModernExtend: true, configure: [async (device) => {
        for (const ep of device.endpoints.filter((x) => x.ID >= 1 && x.ID <= 8)) {
            // Read in small batches so configuration fits a normal APS response.
            for (let start = 0; start < fields.length; start += 4) await ep.read(cluster, fields.slice(start, start + 4).map((x) => x[1]));
            await ep.read(cluster, ['channelRoute', 'bindingStatus', 'bindingClusters']);
        }
        device.powerSource = 'Mains (single phase)'; await device.save();
    }]}],
    endpoint: () => Object.fromEntries(channels.map((name, i) => [name, i + 1])),
    meta: {multiEndpoint: true}, fromZigbee: [received], toZigbee: settings,
    exposes: [
        e.action(channels.flatMap((ch) => actions.map((action) => `${ch}_${action}`))),
        ...channels.flatMap((ch) => [
            ...fields.map(([key, , , , values]) => {
                const expose = key === 'binding_priority' ? e.binary(key, ea.ALL, 'ON', 'OFF') :
                    key === 'invert' ? e.binary(key, ea.ALL, true, false) :
                    typeof values[0] === 'string' ? e.enum(key, ea.ALL, values) :
                        e.numeric(key, ea.ALL).withValueMin(values[0]).withValueMax(values[1]).withValueStep(1);
                expose.withEndpoint(ch).withCategory('config');
                if (descriptions[key]) expose.withDescription(descriptions[key]);
                return expose;
            }),
            e.enum('control_path', ea.STATE, ['binding', 'z2m', 'mixed']).withEndpoint(ch).withCategory('diagnostic'),
            e.enum('binding_status', ea.STATE, ['unknown', 'unbound', 'bound']).withEndpoint(ch).withCategory('diagnostic'),
            e.numeric('binding_clusters', ea.STATE).withEndpoint(ch).withCategory('diagnostic')
                .withDescription('Binding bits: 1 On/Off, 2 Level, 4 Color, 8 Scenes'),
        ]),
    ],
};
