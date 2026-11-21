"""从仓库根目录用 qrenderdoc --python tests/replay_mesh_comparison.py 回放三帧。"""
import hashlib
import json
import struct
import sys
from collections import Counter
from pathlib import Path

import renderdoc as rd


def phase_draws(actions, structure, phase=''):
    for action in actions:
        name = action.GetName(structure)
        if action.flags & rd.ActionFlags.Drawcall:
            yield phase, action
        yield from phase_draws(action.children, structure, name if action.children else phase)


def save_texture(controller, resource, filename, kind):
    image = rd.TextureSave()
    image.resourceId = resource
    image.destType = rd.FileType.PNG
    image.typeCast = kind
    if kind == rd.CompType.Depth:
        image.channelExtract = 0
    image.mip = 0
    image.slice.sliceIndex = 0
    result = controller.SaveTexture(image, str(filename))
    assert result == rd.ResultCode.Succeeded, str(result)


def replay(folder, mode):
    path = folder / (mode + '_capture.rdc')
    capture = rd.OpenCaptureFile()
    assert capture.OpenFile(str(path), '', None) == rd.ResultCode.Succeeded
    options = rd.ReplayOptions()
    options.apiValidation = True
    result, controller = capture.OpenCapture(options, None)
    assert result == rd.ResultCode.Succeeded, str(result)
    try:
        structure = controller.GetStructuredFile()
        draws = list(phase_draws(controller.GetRootActions(), structure))
        phases = {}
        for phase, action in draws:
            phases.setdefault(phase, []).append(action)
        counters = {c: controller.DescribeCounter(c).name for c in controller.EnumerateCounters()}
        requested = [c for c in counters if c in (rd.GPUCounter.SamplesPassed, rd.GPUCounter.PSInvocations)]
        samples = {(r.eventId, r.counter): int(r.value.u64) for r in controller.FetchCounters(requested)} if requested else {}
        messages = controller.GetFrameInfo().debugMessages
        message_counts = Counter(str(m.severity) for m in messages)
        report = {'capture': path.name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                  'capture_message_counts': dict(message_counts), 'counter_names': [counters[c] for c in requested],
                  'capture_diagnostics': [{'event': m.eventId, 'severity': str(m.severity),
                                           'id': m.messageID, 'message': m.description} for m in messages
                                          if m.severity in (rd.MessageSeverity.High, rd.MessageSeverity.Medium)],
                  'phases': {}}
        for phase, actions in phases.items():
            a = actions[-1]
            controller.SetFrameEvent(a.eventId, True)
            pipe = controller.GetPipelineState()
            state = controller.GetGLPipelineState().depthState
            ids = [draw.eventId for draw in actions]
            report['phases'][phase] = {
                'events': ids, 'draws': len(actions),
                'indices_per_draw': sorted({draw.numIndices for draw in actions}),
                'instances_per_draw': sorted({draw.numInstances for draw in actions}),
                'triangles': sum(draw.numIndices * draw.numInstances // 3 for draw in actions),
                'final_draw_state': {'test': state.depthEnable, 'depth_compare': str(state.depthFunction),
                                     'write_depth': state.depthWrites, 'color_masks': [b.writeMask for b in pipe.GetColorBlends()[:3]]},
                'counters_all_draws': {counters[c]: sum(samples[i, c] for i in ids)
                                       for c in requested if all((i,c) in samples for i in ids)}}
        a = draws[-1][1]
        controller.SetFrameEvent(a.eventId, True)
        pipe = controller.GetPipelineState()
        colors = pipe.GetOutputTargets()
        targets = {'color': colors[0].resource, 'entity': colors[1].resource, 'depth': pipe.GetDepthTarget().resource}
        data = {name: bytes(controller.GetTextureData(tex, rd.Subresource(0,0,0))) for name, tex in targets.items()}
        hashes = {name: hashlib.sha256(blob).hexdigest() for name, blob in data.items()}
        # 原始 OpenGL 纹理从底行开始；回放 API 的 PickPixel 则可直接给图上方起算的坐标。
        entity = struct.unpack('<65536i', data['entity'])
        depth = struct.unpack('<65536f', data['depth'])
        counts = Counter(entity)
        assert set(counts) == set(range(-1, 64))
        assert all((value == -1) == (d == 1.0) and d in (0.5,1.0) for value,d in zip(entity,depth))
        report.update({'final_event': a.eventId, 'image_sha256': hashes,
                       'background_pixels': counts[-1], 'covered_pixels': len(entity)-counts[-1],
                       'distinct_entity_ids_excluding_background': 64})
        pixels = []
        for x,y in [(16,15),(16,239),(240,239),(31,31)]:
            value = controller.PickPixel(targets['entity'], x,y, rd.Subresource(0,0,0), rd.CompType.SInt)
            pixels.append({'xy': [x,y], 'entity': value.intValue[0]})
        report['pick_pixel_top_left'] = pixels
        if mode == 'prepass':
            save_texture(controller, targets['color'], folder/'final-color.png', rd.CompType.Float)
            controller.SetFrameEvent(draws[-2][1].eventId,True)
            depth_target = controller.GetPipelineState().GetDepthTarget().resource
            save_texture(controller, depth_target, folder/'prepass-depth.png',rd.CompType.Depth)
            before = bytes(controller.GetTextureData(depth_target,rd.Subresource(0,0,0)))
            assert before == data['depth']
            report['prepass_end_depth_matches_main'] = True
        assert controller.GetFatalErrorStatus() == rd.ResultCode.Succeeded
        return report
    finally:
        controller.Shutdown()
        capture.Shutdown()


folder = Path.cwd()/'output/profiling/mesh'
modes = ('separate','instanced','prepass')
try:
    rows = {mode: replay(folder, mode) for mode in modes}
    assert [sum(p['draws'] for p in rows[m]['phases'].values()) for m in modes] == [64,1,65]
    assert all(rows[m]['image_sha256'] == rows['separate']['image_sha256'] for m in modes)
    report = {'replay_version': rd.GetVersionString(),
              'matched_entire_images': ['RGBA16F color', 'R32I entity', 'D32F depth'], 'captures': rows}
    (folder/'replay.json').write_text(json.dumps(report,indent=2),encoding='utf8')
except BaseException:
    import traceback
    print(traceback.format_exc(),file=sys.stderr)
    # qrenderdoc 会重定向 Python 的 stderr，将具体异常留给无界面运行的调用者。
    (folder/'replay-error.log').write_text(traceback.format_exc(),encoding='utf8')
    sys.exit(1)
sys.exit(0)
