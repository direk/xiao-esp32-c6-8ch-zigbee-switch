// SPDX-License-Identifier: MIT
// Compatible with firmware 2.x. Replace the previous converter; do not load both files.
import {presets as e, access as ea} from 'zigbee-herdsman-converters/lib/exposes';
import {deviceAddCustomCluster, identify} from 'zigbee-herdsman-converters/lib/modernExtend';

const cluster = 'xiaoControl';
const channels = Array.from({length: 24}, (_, i) => `ch${i + 1}`);
const modes = ['toggle', 'momentary', 'multifunction', 'brightness_level', 'brightness_level_up',
    'brightness_level_down', 'move_to_color_temperature', 'move_to_color_temperature_up',
    'move_to_color_temperature_down', 'scene', 'brightness_level_no_onoff',
    'brightness_level_up_no_onoff', 'brightness_level_down_no_onoff', 'scene_cycle'];
const actions = ['off', 'on', 'toggle', 'single', 'double', 'triple', 'hold', 'release',
    'brightness_move_up', 'brightness_move_down', 'brightness_step_up', 'brightness_step_down',
    'brightness_stop', 'color_temperature_move_up', 'color_temperature_move_down',
    'color_temperature_step_up', 'color_temperature_step_down', 'color_temperature_stop', 'scene'];
const fields = [
    ['input_source','inputSource',16,0x20,['none','D0','D1','D2','D3','D4','D5','D8','D9']],
    ['input_gesture','inputGesture',17,0x20,['legacy','single','double','hold']],
    ['input_mode', 'inputMode', 1, 0x20, ['push', 'rocker']],
    ['invert', 'inverted', 2, 0x10, null],
    ['switch_type', 'switchType', 5, 0x20, modes],
    ['switch_actions', 'switchAction', 6, 0x20, ['off', 'on', 'toggle', 'on_off']],
    ['min_level', 'minLevel', 7, 0x20, [1, 254]],
    ['max_level', 'maxLevel', 8, 0x20, [1, 254]],
    ['transition_time', 'transitionTime', 9, 0x21, [0, 600]],
    ['color_temp_min', 'colorMin', 10, 0x21, [1, 1000]],
    ['color_temp_max', 'colorMax', 11, 0x21, [1, 1000]],
    ['scene_id', 'sceneId', 12, 0x20, [0, 255]],
    ['group_id', 'sceneGroup', 13, 0x21, [0, 0xfff7]],
    ['scene_list', 'sceneList', 15, 0x42, []],
    ['binding_priority', 'bindingPriority', 14, 0x10, ['OFF', 'ON']],
];
const resetReasons=['unknown','power_on','external','software','panic','interrupt_watchdog',
    'task_watchdog','watchdog','deep_sleep','brownout','sdio','usb','jtag','efuse','power_glitch','cpu_lockup'];
const diagnostics=[['uptime','uptimeSeconds',0x30,0x23],['reset_reason','resetReason',0x31,0x20],
    ['zigbee_tx_request_errors','txRequestErrors',0x32,0x23]];
const pinNames=['d0','d1','d2','d3','d4','d5','d8','d9'];
const pinFields=[['pin_mode','pinMode',0x40,0x20,['push','rocker']],
    ['pin_invert','pinInvert',0x41,0x10,null],['pin_hold_time','pinHoldTime',0x42,0x21,[250,5000]],
    ['pin_multi_click_time','pinMultiTime',0x43,0x21,[150,1000]]];
const pinSettings=pinNames.flatMap((pin,index)=>pinFields.map(([property,attr,,type,values])=>({
    key:[`${property}_${pin}`],
    convertSet:async(entity,key,value,meta)=>{
        let encoded=value;
        if(type===0x10){if(typeof value!=='boolean')throw new Error('Expected boolean');}
        else if(typeof values[0]==='string'){encoded=values.indexOf(value);if(encoded<0)throw new Error('Invalid pin mode');}
        else if(!Number.isInteger(value) || value<values[0] || value>values[1])throw new Error('Invalid pin timing');
        const endpoint=meta.device.getEndpoint(index+1);
        await endpoint.write(cluster,{[attr]:encoded},{disableDefaultResponse:false});await endpoint.read(cluster,[attr]);
        return {state:{[key]:value}};
    },
    convertGet:async(entity,key,meta)=>{await meta.device.getEndpoint(index+1).read(cluster,[attr]);},
})));
const customCluster = deviceAddCustomCluster(cluster, {
    name: cluster, ID: 0xfc00,
    attributes: Object.fromEntries([
        ...fields.map(([, name, ID, type]) => [name, {name, ID, type, write: true}]),
        ...diagnostics.map(([,name,ID,type])=>[name,{name,ID,type}]),
        ...pinFields.map(([,name,ID,type])=>[name,{name,ID,type,write:true}]),
        ['channelRoute', {name: 'channelRoute', ID: 0x12, type: 0x20}],
        ['bindingStatus', {name: 'bindingStatus', ID: 0x13, type: 0x20}],
        ['bindingClusters', {name: 'bindingClusters', ID: 0x14, type: 0x20}],
        ['event', {name: 'event', ID: 0x20, type: 0x23}],
    ]), commands: {}, commandsResponse: {},
});
const settings = fields.map(([key, attr, , type, values]) => ({
    key: [key],
    convertSet: async (entity, property, value) => {
        if (!entity || entity.ID < 1 || entity.ID > 24) throw new Error('Expected channel endpoint 1..24');
        let encoded;
        if(type === 0x42) {
            if(typeof value!=='string')throw new Error('scene_list must be comma-separated IDs');
            const parts=value.trim()==='' ? [] : value.split(',').map((s)=>s.trim());
            if(parts.length>16 || parts.some((s)=>!/^\d{1,3}$/.test(s) || Number(s)>255))
                throw new Error('scene_list accepts at most 16 decimal scene IDs in 0..255');
            const ids=parts.map(Number);
            if(new Set(ids).size!==ids.length)throw new Error('scene_list contains duplicate scene IDs');
            encoded=ids.join(',');value=encoded;
        } else if (key === 'invert') {
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
        if (ep < 1 || ep > 24) return;
        const out = {}, data = msg.data;
        for (const [key, attr, , , values] of fields) {
            if (data[attr] === undefined) continue;
            out[`${key}_ch${ep}`] = key === 'invert' ? Boolean(data[attr]) :
                key === 'binding_priority' ? (data[attr] ? 'ON' : 'OFF') :
                typeof values[0] === 'string' ? values[data[attr]] : data[attr];
        }
        if(ep<=8)for(const [key,attr,,,values] of pinFields)
            if(data[attr]!==undefined)out[`${key}_${pinNames[ep-1]}`]=key==='pin_invert'?Boolean(data[attr]):
                key==='pin_mode'?values[data[attr]]:data[attr];
        if(ep===1)for(const [key,attr] of diagnostics)
            if(data[attr]!==undefined)out[key]=key==='reset_reason' ? (resetReasons[data[attr]] ?? 'unknown') : data[attr];
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
const diagnosticReports={cluster:'haDiagnostic',type:['readResponse','attributeReport'],
    convert: (model,msg)=>msg.endpoint.ID===1 && msg.data.numberOfResets!==undefined ?
        {number_of_resets:msg.data.numberOfResets} : undefined};
const diagnosticSettings={key:['number_of_resets',...diagnostics.map((x)=>x[0])],
    convertGet:async(entity,key,meta)=>{
        const endpoint=meta.device.getEndpoint(1);
        if(key==='number_of_resets')await endpoint.read('haDiagnostic',['numberOfResets']);
        else await endpoint.read(cluster,[diagnostics.find((x)=>x[0]===key)[1]]);
    }};
const descriptions = {
    input_source: 'Physical source pin, or none for UART/USB only. D6/D7 are reserved for UART. Saved on device.',
    input_gesture: 'legacy preserves raw-contact behavior. single/double activate this endpoint once; hold activates until release. A pin cannot mix legacy and decoded mappings. Set the original endpoint to single before adding double/hold mappings.',
    input_mode: 'Physical input. rocker+toggle sends Toggle on both edges; rocker+momentary follows closed=ON/open=OFF. Advanced modes require push.',
    switch_type: 'Per-channel logical function. Brightness-only modes never send On/Off.',
    switch_actions: 'Push toggle/momentary: off, on, toggle, or on_off (alternating explicit On/Off, first On after reboot or writing this option). Unrelated settings retain the sequence. Momentary alternates on press and release. Rocker ignores this setting.',
    transition_time: 'Transition duration in 0.1 seconds; default 25 = 2.5 seconds.',
    binding_priority: 'Default ON: direct binding suppresses action. OFF: direct control also sends action. Saved on device.',
    group_id: 'Direct Zigbee scene group; scene_cycle requires 1..65527. In scene mode, 0 uses Scenes bindings.',
    scene_list: 'scene_cycle: up to 16 distinct scene IDs (0..255) in click order, e.g. 0,1,3. Empty disables output. Saved on device. List/group/mode writes and reboot restart at first entry. Scenes must already exist in the target group.',
};
export default {
    zigbeeModel: ['XIAO-C6-8CH', 'XIAO-C6-4CH-FAIL1'],
    model: 'XIAO-C6-8CH', vendor: 'DIY-XIAO',
    description: 'Eight-input / 24-endpoint mains-powered Zigbee switch/dimmer/scene controller with Zigbee OTA and two antenna variants',
    version: '0.1.0',
    ota: {modelId: 'XIAO-C6-8CH', manufacturerName: 'DIY-XIAO'},
    extend: [customCluster, identify(), {isModernExtend: true, configure: [async (device) => {
        for (const ep of device.endpoints.filter((x) => x.ID >= 1 && x.ID <= 24)) {
            // Read long text separately to keep configuration responses bounded.
            const small=fields.filter((x)=>x[3]!==0x42);
            for (let start = 0; start < small.length; start += 4) await ep.read(cluster, small.slice(start, start + 4).map((x) => x[1]));
            await ep.read(cluster,['sceneList']);
            if(ep.ID<=8)await ep.read(cluster,pinFields.map((x)=>x[1]));
            await ep.read(cluster, ['channelRoute', 'bindingStatus', 'bindingClusters']);
        }
        await device.getEndpoint(1).read('haDiagnostic',['numberOfResets']);
        await device.getEndpoint(1).read(cluster,diagnostics.map((x)=>x[1]));
        device.powerSource = 'Mains (single phase)'; await device.save();
    }]}],
    endpoint: () => Object.fromEntries(channels.map((name, i) => [name, i + 1])),
    meta: {multiEndpoint: true}, fromZigbee: [received,diagnosticReports], toZigbee: [...settings,...pinSettings,diagnosticSettings],
    exposes: [
        ...pinNames.flatMap((pin)=>pinFields.map(([key,,,type,values])=>{
            const name=`${key}_${pin}`;
            const expose=type===0x10?e.binary(name,ea.ALL,true,false):typeof values[0]==='string'?e.enum(name,ea.ALL,values):
                e.numeric(name,ea.ALL).withValueMin(values[0]).withValueMax(values[1]).withValueStep(1).withUnit('ms');
            return expose.withCategory('config').withDescription(`Shared electrical/gesture setting for ${pin.toUpperCase()}; affects all endpoints assigned to this pin.`);
        })),
        e.numeric('number_of_resets',ea.STATE_GET).withCategory('diagnostic')
            .withDescription('Persistent restart count since first installation: initially 0, saturates at 65535. Network/config reset retains it.'),
        e.numeric('uptime',ea.STATE_GET).withUnit('s').withCategory('diagnostic'),
        e.enum('reset_reason',ea.STATE_GET,resetReasons).withCategory('diagnostic'),
        e.numeric('zigbee_tx_request_errors',ea.STATE_GET).withCategory('diagnostic')
            .withDescription('Direct-control APS requests rejected locally since boot. Does not measure lamp delivery or group acknowledgements.'),
        e.action(channels.flatMap((ch) => actions.map((action) => `${ch}_${action}`))),
        ...channels.flatMap((ch) => [
            ...fields.map(([key, , , type, values]) => {
                const expose = type===0x42 ? e.text(key,ea.ALL) : key === 'binding_priority' ? e.binary(key, ea.ALL, 'ON', 'OFF') :
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
