import assert from 'node:assert/strict';
import {test} from 'node:test';
import {prepareDefinition} from 'zigbee-herdsman-converters';
import {Zcl} from 'zigbee-herdsman';
import source from '../zigbee2mqtt/xiao-c6-8ch.mjs';

const definition = prepareDefinition(source);
const fz = definition.fromZigbee.find((c) => c.cluster === 'xiaoControl');
// Mirror Z2M publish.ts dispatch: strip endpoint suffix before converter lookup,
// invoke each converter at most once per endpoint, then suffix optimistic state.
async function dispatch(message, meta, operation = 'convertSet') {
    const used = new Map();
    const published = {};
    for (const [property, value] of Object.entries(message)) {
        const match = /^(.*?)_(ch(?:[1-9]|1[0-9]|2[0-4]))$/.exec(property);
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
    const endpoints = Array.from({length: 24}, (_, i) => ({
        ID: i + 1,
        attributes: {},
        saveClusterAttributeKeyValue(cluster, values) { Object.assign(this.attributes, values); },
        getClusterAttributeValue(cluster, name) { return this.attributes[name]; },
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
        command: async (cluster, name, payload, options) => {
            const frame=Zcl.Frame.create(1,0,false,undefined,1,name,cluster,payload,customClusters);
            const bytes=frame.toBuffer();
            const parsed=Zcl.Frame.fromBuffer(frame.cluster.ID,Zcl.Header.fromBuffer(bytes),bytes,customClusters);
            calls.push({kind:'command',ep:i+1,cluster,name,payload:parsed.payload,options});
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
    assert.deepEqual([...new Set(calls.filter((x) => x.kind === 'read').map((x) => x.ep))], Array.from({length:24},(_,i)=>i+1));
    assert.deepEqual(definition.endpoint(device), Object.fromEntries(Array.from({length:24},(_,i)=>[`ch${i+1}`,i+1])));
    assert.equal(definition.exposes(undefined, {}).find((x) => x.name === 'action').values.length, 456);
});

test('each setting reaches only its channel, with real Zigbee attribute encoding', async () => {
    const {device, calls, meta} = fixture();
    await definition.configure(device, {}, definition);
    for (let ep = 1; ep <= 8; ++ep) {
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

test('removed heartbeat stays absent with global device diagnostics', async () => {
    const {device, meta} = fixture();
    await definition.configure(device, {}, definition);
    for (const key of ['heartbeat', 'fallback_timeout']) {
        assert.ok(!definition.toZigbee.some((c) => c.key?.includes(key)));
    }
    const schema = device.customClusters.xiaoControl;
    for (const name of ['heartbeat', 'timeout', 'challenge', 'route']) assert.equal(schema.attributes[name], undefined);
    for (const name of ['heartbeat', 'heartbeat_challenge', 'fallback_timeout', 'z2m_online']) {
        assert.ok(!definition.exposes(undefined, {}).some((e) => e.name === name));
    }
    assert.equal(definition.fromZigbee.length, 2);
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
    for (let i = 1; i <= 8; ++i) {
        const expose = definition.exposes(undefined, {}).find((x) => x.property === `input_mode_ch${i}`);
        assert.equal(expose.name, 'input_mode');
        assert.equal(expose.endpoint, `ch${i}`);
    }
});

test('eight channels and original On/Off commands decode independently, duplicate reports are suppressed', () => {
    for (let ep = 1; ep <= 8; ++ep) {
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
    assert.equal(fz.convert(definition, {endpoint: {ID: 25}, type: 'attributeReport', data: {event: 258}}), undefined);
    assert.equal(fz.convert(definition, {endpoint, type: 'attributeReport', data: {event: 300}}), undefined);
});

test('actual firmware-format attribute report parses with the registered custom schema', async () => {
    const {device} = fixture();
    await definition.configure(device, {}, definition);
    const bytes = Buffer.from([0x18, 0x7a, 0x0a, 0x12, 0x00, 0x20, 0x00]);
    const parsed = Zcl.Frame.fromBuffer(0xfc00, Zcl.Header.fromBuffer(bytes), bytes, device.customClusters);
    assert.equal(parsed.cluster.name, 'xiaoControl');
    const data = Object.fromEntries(parsed.payload.map((a) => [Zcl.Utils.getClusterAttribute(parsed.cluster, a.attrId, undefined).name, a.attrData]));
    assert.deepEqual(fz.convert(definition, {endpoint: {ID: 1}, type: 'attributeReport', data}), {control_path_ch1: 'binding'});
});

test('independent binding and route diagnostics decode on all eight endpoints', () => {
    for (let ep = 1; ep <= 8; ++ep) {
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

test('all fourteen mode IDs and uint16 settings encode correctly on eight endpoints', async () => {
    const {device, calls, meta} = fixture();
    await definition.configure(device, {}, definition);
    const modes = definition.exposes(undefined, {}).find((e) => e.property === 'switch_type_ch1').values;
    assert.equal(modes.length, 14);
    for (let ep = 1; ep <= 8; ++ep) {
        for (let mode = 0; mode < modes.length; ++mode) {
            await dispatch({[`switch_type_ch${ep}`]: modes[mode]}, meta);
            const write = calls.findLast((x) => x.kind === 'write');
            assert.equal(write.ep, ep); assert.equal(write.parsed[0].attrId, 5); assert.equal(write.parsed[0].attrData, mode);
        }
        await dispatch({[`transition_time_ch${ep}`]: 600, [`group_id_ch${ep}`]: 0xfff7}, meta);
        const writes = calls.filter((x) => x.kind === 'write').slice(-2);
        assert.equal(writes[0].parsed[0].dataType, 0x21); assert.equal(writes[0].parsed[0].attrData, 600);
        assert.equal(writes[1].parsed[0].attrData, 0xfff7);
    }
    for (const [key, value] of [['transition_time_ch8',601],['group_id_ch8',0xfff8],['min_level_ch8',0],['color_temp_max_ch8',1001]]) {
        await assert.rejects(dispatch({[key]:value},meta));
    }
});

test('production C gesture frames decode as standard Level/Color/Scene commands in herdsman', async () => {
    const {execFileSync} = await import('node:child_process');
    const {mkdtempSync, rmSync} = await import('node:fs');
    const {tmpdir} = await import('node:os');
    const {join} = await import('node:path');
    const dir = mkdtempSync(join(tmpdir(), 'xiao-frame-test-'));
    try {
        const exe = join(dir,'frames');
        execFileSync('cc',['-std=c11','-Wall','-Wextra','-Werror','-I','firmware/main',
            'firmware/main/control.c','tests/frame_harness.c','-o',exe]);
        const lines = execFileSync(exe,{encoding:'utf8'}).trim().split('\n');
        assert.ok(lines.length > 30);
        const safeKinds = new Set();
        let colors=0, scenes=0, rockerEdges=0, alternating=0, cycling=0;
        for (const line of lines) {
            const [modeText,phase,clusterText,hex] = line.split(' ');
            const mode=Number(modeText),cluster=Number(clusterText),bytes=Buffer.from(hex,'hex');
            const parsed=Zcl.Frame.fromBuffer(cluster,Zcl.Header.fromBuffer(bytes),bytes,{});
            if (phase==='on_off') {
                assert.equal(cluster,6);assert.equal(parsed.command.name,alternating++%2 ? 'off' : 'on');
                assert.deepEqual(parsed.payload,{});
            }
            if (phase==='rocker_edge') {
                ++rockerEdges;assert.equal(cluster,6);assert.equal(parsed.command.ID,2);
                assert.equal(parsed.command.name,'toggle');assert.deepEqual(parsed.payload,{});
            }
            if (mode>=10 && mode<=12) {
                assert.equal(cluster,8); assert.ok([0,2,3].includes(parsed.command.ID));
                assert.equal(parsed.payload.optionsMask,1); assert.equal(parsed.payload.optionsOverride,0);
                safeKinds.add(parsed.command.ID);
            }
            if (cluster===8 && phase==='double') assert.equal(parsed.payload.stepsize,25);
            if (cluster===0x300) {
                ++colors;
                if(parsed.command.ID===0x4c) {
                    assert.equal(parsed.payload.stepsize,87);assert.equal(parsed.payload.minimum,153);assert.equal(parsed.payload.maximum,500);
                }
            }
            if(cluster===5) {
                ++scenes;assert.equal(parsed.command.ID,5);
                assert.equal(parsed.payload.groupid,mode===13 ? 321 : 123);
                assert.equal(parsed.payload.sceneid,mode===13 ? [3,0,1,3][cycling++] : 8);
            }
        }
        assert.deepEqual([...safeKinds].sort(),[0,2,3]);assert.ok(colors>0 && scenes>0);assert.equal(rockerEdges,4);assert.equal(alternating,4);assert.equal(cycling,4);
    } finally {rmSync(dir,{recursive:true,force:true});}
});

test('all 152 channel/action combinations publish the matching gesture or lighting action', () => {
    const values=definition.exposes(undefined, {}).find((e)=>e.name==='action').values;
    for(let ep=1;ep<=8;++ep){
        const endpoint={ID:ep},names=values.filter((name)=>name.startsWith(`ch${ep}_`));
        assert.equal(names.length,19);
        for(let id=0;id<19;++id){
            assert.deepEqual(fz.convert(definition,{endpoint,type:'attributeReport',data:{event:((id+1)<<8)|id}}),{action:names[id]});
        }
    }
});


test('binding priority is independently writable/readable on all eight endpoints as ON/OFF', async () => {
    const {device, calls, meta} = fixture();
    await definition.configure(device, {}, definition);
    for (let ep = 1; ep <= 8; ++ep) {
        assert.ok(calls.some((x) => x.kind === 'read' && x.ep === ep && x.attributes.includes('bindingPriority')));
        const expose = definition.exposes(undefined, {}).find((x) => x.property === `binding_priority_ch${ep}`);
        assert.equal(expose.value_on, 'ON');
        assert.equal(expose.value_off, 'OFF');
        for (const [value, encoded] of [['ON', 1], ['OFF', 0]]) {
            assert.deepEqual(await tz.convertSet(null, `binding_priority_ch${ep}`, value, meta),
                {state: {[`binding_priority_ch${ep}`]: value}});
            const write = calls.findLast((x) => x.kind === 'write');
            assert.equal(write.ep, ep);
            assert.deepEqual(write.parsed, [{attrId: 14, dataType: 0x10, attrData: encoded}]);
            for (const type of ['readResponse', 'attributeReport']) {
                assert.deepEqual(fz.convert(definition, {endpoint: device.getEndpoint(ep), type, data: {bindingPriority: encoded}}),
                    {[`binding_priority_ch${ep}`]: value});
            }
        }
        await tz.convertGet(null, `binding_priority_ch${ep}`, meta);
        assert.deepEqual(calls.at(-1).attributes, ['bindingPriority']);
    }
    for (const bad of [true, false, 'unknown', 2, null])
        await assert.rejects(tz.convertSet(null, 'binding_priority_ch1', bad, meta));
    assert.deepEqual(await dispatch({binding_priority_ch1:'OFF', binding_priority_ch2:'ON', invert_ch1:true}, meta),
        {state:{binding_priority_ch1:'OFF', binding_priority_ch2:'ON', invert_ch1:true}});
});


test('on_off setting encodes and reads back on every endpoint without a new action ID', async () => {
    const {device,calls,meta}=fixture();await definition.configure(device,{},definition);
    for(let ep=1;ep<=8;++ep) {
        const key=`switch_actions_ch${ep}`;
        assert.deepEqual(definition.exposes(undefined, {}).find((x)=>x.property===key).values,['off','on','toggle','on_off']);
        await tz.convertSet(null,key,'on_off',meta);
        const write=calls.findLast((x)=>x.kind==='write');
        assert.equal(write.ep,ep);
        assert.deepEqual(write.parsed,[{attrId:6,dataType:0x20,attrData:3}]);
        for(const type of ['readResponse','attributeReport'])
            assert.deepEqual(fz.convert(definition,{endpoint:device.getEndpoint(ep),type,data:{switchAction:3}}),{[key]:'on_off'});
    }
});


test('scene_cycle and text lists write/read independently on eight endpoints with real ZCL strings', async () => {
    const {device,calls,meta}=fixture();await definition.configure(device,{},definition);
    for(let ep=1;ep<=8;++ep) {
        await tz.convertSet(null,`switch_type_ch${ep}`,'scene_cycle',meta);
        assert.equal(calls.findLast((x)=>x.kind==='write').parsed[0].attrData,13);
        const key=`scene_list_ch${ep}`;
        assert.equal(definition.exposes(undefined, {}).find((x)=>x.property===key).type,'text');
        const result=await tz.convertSet(null,key,' 3, 0, 001 ',meta);
        assert.deepEqual(result,{state:{[key]:'3,0,1'}});
        const write=calls.findLast((x)=>x.kind==='write');assert.equal(write.ep,ep);
        assert.deepEqual(write.parsed,[{attrId:15,dataType:0x42,attrData:'3,0,1'}]);
        assert.deepEqual(fz.convert(definition,{endpoint:device.getEndpoint(ep),type:'readResponse',data:{sceneList:'3,0,1'}}),{[key]:'3,0,1'});
        await tz.convertSet(null,key,'',meta);
        assert.equal(calls.findLast((x)=>x.kind==='write').parsed[0].attrData,'');
    }
    const maximum=Array.from({length:16},(_,i)=>240+i).join(',');
    await tz.convertSet(null,'scene_list_ch1',maximum,meta);
    for(const invalid of ['0,0','1,01','1,','256','-1','1.5','0x01',',1','a',123,Array.from({length:17},(_,i)=>i).join(',')])
        await assert.rejects(tz.convertSet(null,'scene_list_ch1',invalid,meta));
});


test('global Identify sends a real genIdentify command to endpoint 1', async()=>{
    const {device,calls,meta}=fixture();await definition.configure(device,{},definition);
    const converter=definition.toZigbee.find((x)=>x.key?.includes('identify'));
    await converter.convertSet(device.getEndpoint(1),'identify','identify',{...meta,mapped:definition,options:{identify_timeout:5}});
    const command=calls.findLast((x)=>x.kind==='command');
    assert.equal(command.ep,1);assert.equal(command.cluster,'genIdentify');
    assert.equal(command.name,'identify');assert.equal(command.payload.identifytime,5);
});

test('global diagnostics use standard resets plus read-only custom attributes on endpoint 1', async()=>{
    const {device,calls,meta}=fixture();await definition.configure(device,{},definition);
    const converter=definition.fromZigbee.find((x)=>x.cluster==='haDiagnostic');
    const bytes=Zcl.Frame.create(0,1,true,undefined,1,'report','haDiagnostic',
        [{attrId:0,dataType:0x21,attrData:65535}],{}).toBuffer();
    const parsed=Zcl.Frame.fromBuffer(0x0b05,Zcl.Header.fromBuffer(bytes),bytes,{});
    assert.equal(parsed.payload[0].attrData,65535);
    for(const type of ['readResponse','attributeReport']) {
        assert.deepEqual(converter.convert(definition,{endpoint:device.getEndpoint(1),type,data:{numberOfResets:65535}}),{number_of_resets:65535});
        assert.equal(converter.convert(definition,{endpoint:device.getEndpoint(2),type,data:{numberOfResets:1}}),undefined);
        assert.deepEqual(fz.convert(definition,{endpoint:device.getEndpoint(1),type,
            data:{uptimeSeconds:4294967295,resetReason:9,txRequestErrors:4294967295}}),
            {uptime:4294967295,reset_reason:'brownout',zigbee_tx_request_errors:4294967295});
    }
    assert.equal(fz.convert(definition,{endpoint:device.getEndpoint(2),type:'readResponse',data:{resetReason:9}}),undefined);
    for(const key of ['number_of_resets','uptime','reset_reason','zigbee_tx_request_errors']) {
        const expose=definition.exposes(undefined, {}).find((x)=>x.property===key);
        assert.ok(expose && expose.access===5 && !expose.endpoint);
        await tz.convertGet(null,key,meta);assert.equal(calls.at(-1).ep,1);
    }
    assert.ok(calls.some((x)=>x.kind==='read' && x.ep===1 && x.attributes.includes('numberOfResets')));
    assert.ok(calls.some((x)=>x.kind==='read' && x.ep===1 && x.attributes.includes('resetReason')));
});


test('24 endpoints expose independent source/gesture/function/bindings with unchanged legacy names',async()=>{
    const {device,calls,meta}=fixture();await definition.configure(device,{},definition);
    for(let ep=1;ep<=24;++ep) {
        for(const [key,value,id,encoded] of [['input_source','D9',16,8],['input_gesture','hold',17,3],['binding_priority','OFF',14,0]]) {
            await tz.convertSet(null,`${key}_ch${ep}`,value,meta);
            const write=calls.findLast((x)=>x.kind==='write');
            assert.equal(write.ep,ep);assert.equal(write.parsed[0].attrId,id);assert.equal(Number(write.parsed[0].attrData),encoded);
        }
        assert.deepEqual(fz.convert(definition,{endpoint:device.getEndpoint(ep),type:'readResponse',data:{inputSource:8,inputGesture:3}}),
            {[`input_source_ch${ep}`]:'D9',[`input_gesture_ch${ep}`]:'hold'});
        assert.equal(definition.exposes(undefined, {}).find((e)=>e.property===`input_source_ch${ep}`).values.includes('D6'),false);
    }
    assert.equal(definition.exposes(undefined, {}).find((e)=>e.name==='action').values.length,456);
    for(let ep=9;ep<=24;++ep)assert.deepEqual(fz.convert(definition,{endpoint:device.getEndpoint(ep),type:'attributeReport',data:{event:0x123402}}),{action:`ch${ep}_toggle`});
    const pins=['d0','d1','d2','d3','d4','d5','d8','d9'];
    for(let i=0;i<8;++i) {
        for(const [key,value,id,encoded] of [['pin_mode','push',64,0],['pin_invert',true,65,1],['pin_hold_time',1200,66,1200],['pin_multi_click_time',300,67,300]]) {
            await tz.convertSet(null,`${key}_${pins[i]}`,value,meta);
            const write=calls.findLast((x)=>x.kind==='write');assert.equal(write.ep,i+1);assert.equal(write.parsed[0].attrId,id);assert.equal(Number(write.parsed[0].attrData),encoded);
        }
        assert.deepEqual(fz.convert(definition,{endpoint:device.getEndpoint(i+1),type:'readResponse',data:{pinMode:0,pinInvert:true,pinHoldTime:1200,pinMultiTime:300}}),
            {[`pin_mode_${pins[i]}`]:'push',[`pin_invert_${pins[i]}`]:true,[`pin_hold_time_${pins[i]}`]:1200,[`pin_multi_click_time_${pins[i]}`]:300});
    }
});

test('switch_actions visibility follows mode/type independently on all 24 endpoints', () => {
    const {device} = fixture();
    const visible = (ep) => definition.exposes(device, {}).some((x) => x.property === `switch_actions_ch${ep}`);
    const modes = definition.exposes(undefined, {}).find((x) => x.property === 'switch_type_ch1').values;
    let changes = 0;
    const meta = {device, deviceExposesChanged: () => { ++changes; }};
    const report = (ep, data, type = 'attributeReport') => {
        const endpoint = device.getEndpoint(ep);
        // The real controller updates the attribute cache BEFORE invoking converters.
        endpoint.saveClusterAttributeKeyValue('xiaoControl', data);
        return fz.convert(definition, {endpoint, type, data}, undefined, {}, meta);
    };
    assert.ok(Array.from({length:24}, (_,i) => visible(i+1)).every(Boolean));
    for (let ep = 1; ep <= 24; ++ep) {
        for (let type = 0; type < modes.length; ++type) {
            const before = visible(ep), count = changes;
            report(ep, {inputMode: 0, switchType: type}, 'readResponse');
            const expected = type <= 1;
            assert.equal(visible(ep), expected, `EP${ep} push/${modes[type]}`);
            assert.ok(changes-count >= Number(before !== expected), 'refresh whenever action visibility changes');
            const unchanged = changes;
            report(ep, {inputMode: 0, switchType: type});
            report(ep, {switchAction: 2});
            assert.equal(changes, unchanged, 'no refresh for repeated settings or unrelated attributes');
        }
        report(ep, {switchType: 0});
        assert.equal(visible(ep), true);
        report(ep, {inputMode: 1});
        assert.equal(visible(ep), false, 'rocker ignores actions even with toggle');
        report(ep, {switchType: 1});
        assert.equal(visible(ep), false, 'rocker ignores actions even with momentary');
        report(ep, {inputMode: 0});
        assert.equal(visible(ep), true);
        if (ep < 24) assert.equal(visible(ep+1), true, 'other endpoint unaffected');
    }
});

test('cached settings determine exposes after restart; partial reads and notifications restore the field', () => {
    const {device} = fixture();
    const ep = device.getEndpoint(1);
    ep.saveClusterAttributeKeyValue('xiaoControl', {switchType:13, inputMode:0});
    const has = () => definition.exposes(device, {}).some((x) => x.property === 'switch_actions_ch1');
    assert.equal(has(), false, 'persisted scene_cycle cache hides field before any new message');
    let changes = 0;
    const meta = {device, deviceExposesChanged: () => { ++changes; assert.equal(has(), true); }};
    const result = fz.convert(definition, {endpoint:ep, type:'readResponse', data:{switchType:1}}, undefined, {}, meta);
    assert.deepEqual(result, {switch_type_ch1:'momentary'});
    assert.equal(changes, 1);
    assert.equal(ep.getClusterAttributeValue('xiaoControl','inputMode'), 0, 'partial response retains input mode');
});

test('changing shared pin mode reads all endpoint aliases; remapping reads the new effective mode', async () => {
    const {device,calls,meta} = fixture();
    await definition.configure(device,{},definition);
    calls.length = 0;
    await dispatch({pin_mode_d0:'rocker'},meta);
    assert.deepEqual(calls.filter((x)=>x.kind==='read' && x.attributes.length===1 && x.attributes[0]==='inputMode').map((x)=>x.ep),
        Array.from({length:24},(_,i)=>i+1));
    calls.length = 0;
    await dispatch({input_source_ch24:'D1'},meta);
    assert.deepEqual(calls.at(-1),{kind:'read',ep:24,attributes:['inputSource','inputMode']});
});

test('all conditional fields follow the complete mode matrix on 24 independent endpoints', () => {
    const {device} = fixture();
    const expectedTypes = {
        switch_actions:[0,1], min_level:[3,4,5,10,11,12], max_level:[3,4,5,10,11,12],
        color_temp_min:[6,7,8], color_temp_max:[6,7,8], transition_time:[3,4,5,6,7,8,10,11,12],
        scene_id:[9], group_id:[9,13], scene_list:[13],
    };
    const always = ['input_source','input_gesture','input_mode','invert','switch_type','binding_priority'];
    const properties = () => new Set(definition.exposes(device, {}).map((x)=>x.property));
    const signature = (set, ep) => Object.keys(expectedTypes).map((key)=>Number(set.has(`${key}_ch${ep}`))).join('');
    let notifications = 0;
    const meta = {device, deviceExposesChanged:()=>{++notifications;}};
    for (let ep = 1; ep <= 24; ++ep) {
        const endpoint = device.getEndpoint(ep);
        for (let type = 0; type < 14; ++type) {
            const before = properties(), count = notifications;
            // Simulate the real receive pipeline, which updates cache first.
            const data = {inputMode:0, switchType:type};
            endpoint.saveClusterAttributeKeyValue('xiaoControl',data);
            fz.convert(definition,{endpoint,type:'readResponse',data},undefined,{},meta);
            const after = properties();
            for (const [key,types] of Object.entries(expectedTypes))
                assert.equal(after.has(`${key}_ch${ep}`),types.includes(type),`${key} EP${ep} mode ${type}`);
            for (const key of always) assert.ok(after.has(`${key}_ch${ep}`));
            assert.equal(notifications-count, Number(signature(before,ep)!==signature(after,ep)));
            for (let other = 1; other <= 24; ++other)
                if (other!==ep) assert.equal(signature(before,other),signature(after,other));
            const unchanged = notifications;
            fz.convert(definition,{endpoint,type:'attributeReport',data},undefined,{},meta);
            assert.equal(notifications, unchanged);
        }
    }
});

test('hiding fields preserves cached values and failed type writes do not change visibility', async () => {
    const {device,meta} = fixture();
    await definition.configure(device,{},definition);
    const endpoint = device.getEndpoint(24);
    const saved = {minLevel:17,maxLevel:201,sceneId:3,sceneGroup:42,sceneList:'0,1,3',switchAction:3};
    endpoint.saveClusterAttributeKeyValue('xiaoControl',{...saved,inputMode:0,switchType:13});
    const keys = () => definition.exposes(device,{}).filter((x)=>x.endpoint==='ch24').map((x)=>x.property);
    const before = keys();
    endpoint.write = async()=>{throw new Error('Simulated failed Zigbee write');};
    await assert.rejects(dispatch({switch_type_ch24:'brightness_level'},meta),/failed Zigbee write/);
    assert.deepEqual(keys(),before);
    for (const type of [3,9,13]) {
        fz.convert(definition,{endpoint,type:'readResponse',data:{switchType:type}},undefined,{},meta);
        for (const [key,value] of Object.entries(saved))
            assert.equal(endpoint.getClusterAttributeValue('xiaoControl',key),value);
    }
    assert.ok(keys().includes('scene_list_ch24'));
    assert.ok(!keys().includes('scene_id_ch24'));
});
