"""在仓库根目录用 qrenderdoc --python 执行，实际回放 Cornell 场景截帧。"""
import hashlib
import json
import struct
import sys
import traceback
from pathlib import Path
import renderdoc as rd

output = Path.cwd() / 'output/scenes'
path = output / 'CornellBox_capture.rdc'
report = {'capture': path.name, 'tool': 'RenderDoc 1.47', 'api_validation': True,
          'timing': 'not measured', 'status': 'running'}
capture = None
controller = None


def draws(actions):
    for action in actions:
        if action.flags & rd.ActionFlags.Drawcall:
            yield action
        yield from draws(action.children)


def messages(items):
    return [{'event': m.eventId, 'severity': str(m.severity), 'category': str(m.category),
             'description': m.description} for m in items]


try:
    report['sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
    capture = rd.OpenCaptureFile()
    result = capture.OpenFile(str(path), '', None)
    assert result == rd.ResultCode.Succeeded, str(result)
    options = rd.ReplayOptions()
    options.apiValidation = True
    result, controller = capture.OpenCapture(options, None)
    assert result == rd.ResultCode.Succeeded, str(result)
    report['capture_messages'] = messages(controller.GetFrameInfo().debugMessages)
    textures = {texture.resourceId: texture for texture in controller.GetTextures()}
    structure = controller.GetStructuredFile()
    actions = list(draws(controller.GetRootActions()))
    report['draws'] = []
    report['groups'] = []
    previous = None
    entity = None

    for action in actions:
        controller.SetFrameEvent(action.eventId, True)
        pipe = controller.GetPipelineState()
        gl = controller.GetGLPipelineState()
        targets = [target.resource for target in pipe.GetOutputTargets()]
        depth = pipe.GetDepthTarget().resource
        formats = [textures[target].format.Name() if target in textures else None for target in targets]
        masks = [blend.writeMask for blend in pipe.GetColorBlends()[:len(targets)]]
        row = {'event': action.eventId, 'name': action.GetName(structure),
               'indices': action.numIndices, 'instances': action.numInstances,
               'vertex_shader': str(pipe.GetShader(rd.ShaderStage.Vertex)),
               'fragment_shader': str(pipe.GetShader(rd.ShaderStage.Fragment)),
               'colors': [str(target) for target in targets], 'formats': formats,
               'depth': str(depth), 'depth_test': gl.depthState.depthEnable,
               'depth_write': gl.depthState.depthWrites,
               'depth_function': str(gl.depthState.depthFunction), 'color_masks': masks}
        report['draws'].append(row)
        key = (row['vertex_shader'], row['fragment_shader'], tuple(row['colors']), tuple(masks), row['depth'])
        if key != previous:
            report['groups'].append({'first_event': action.eventId, 'last_event': action.eventId,
                                     'draws': 0, 'indices': 0, 'formats': formats,
                                     'color_masks': masks, 'depth': str(depth)})
            previous = key
        group = report['groups'][-1]
        group['last_event'] = action.eventId
        group['draws'] += 1
        group['indices'] += action.numIndices * max(1, action.numInstances)
        if len(targets) > 1 and targets[1] in textures and textures[targets[1]].format.compType == rd.CompType.SInt:
            entity = targets[1]

    # Cornell 有 5 个子网格，共 96 个索引；四个几何阶段都应实际绘制它们。
    geometry = [group for group in report['groups'] if group['draws'] == 5 and group['indices'] == 96]
    assert len(geometry) == 4, geometry
    report['geometry_groups_verified'] = 4
    assert entity is not None, 'Entity attachment missing'
    data = bytes(controller.GetTextureData(entity, rd.Subresource(0, 0, 0)))
    ids = struct.unpack('<' + str(len(data) // 4) + 'i', data)
    visible = sum(value != -1 for value in ids)
    assert visible > 100, 'No model pixels after replay'
    report['geometry_pixels'] = visible
    report['entity_values'] = sorted(set(ids))

    final = controller.GetPipelineState().GetOutputTargets()[0].resource
    texture = textures[final]
    report['final_texture'] = {'event': actions[-1].eventId, 'resource': str(final),
                               'width': texture.width, 'height': texture.height,
                               'format': texture.format.Name()}
    save = rd.TextureSave()
    save.resourceId = final
    save.destType = rd.FileType.PNG
    save.mip = 0
    save.slice.sliceIndex = 0
    result = controller.SaveTexture(save, str(output / 'CornellBox-replay.png'))
    assert result == rd.ResultCode.Succeeded, str(result)
    report['replay_messages'] = messages(controller.GetDebugMessages())
    report['fatal_error_status'] = str(controller.GetFatalErrorStatus())
    report['status'] = 'passed'
except BaseException:
    report['status'] = 'failed'
    report['error'] = traceback.format_exc()
finally:
    if controller:
        controller.Shutdown()
    if capture:
        capture.Shutdown()
    (output / 'CornellBox-replay.json').write_text(json.dumps(report, indent=2), encoding='utf-8')

# 完成脚本后退出，避免打开第二个 GUI 干扰当前桌面。
sys.exit(0 if report['status'] == 'passed' else 1)
