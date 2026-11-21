"""在项目根目录通过 qrenderdoc --python 运行；从保存的 RDC 实际回放并导出附件。"""
import json
import hashlib
import struct
import sys
import traceback
from pathlib import Path
import renderdoc as rd

output = Path.cwd() / 'output/captures'
report = {'source': 'DepthPrepass_capture.rdc', 'coordinate_origin': 'top-left',
          'method': 'RenderDoc ReplayController.SetFrameEvent / SaveTexture / PickPixel',
          'tool_version': '1.47', 'api_validation': True, 'gpu_timing': 'not measured',
          'status': 'running'}
capture = None
controller = None
subresource = rd.Subresource(0, 0, 0)


def draws(actions):
    for action in actions:
        if action.flags & rd.ActionFlags.Drawcall:
            yield action
        yield from draws(action.children)


def messages(items):
    return [{'event': m.eventId, 'source': str(m.source), 'severity': str(m.severity),
             'category': str(m.category), 'id': m.messageID, 'description': m.description}
            for m in items]


def collect_messages(step):
    report['replay_messages'].append({'step': step, 'messages': messages(controller.GetDebugMessages())})


def save_texture(resource, filename, integer=False):
    save = rd.TextureSave()
    save.resourceId = resource
    save.destType = rd.FileType.DDS if filename.endswith('.dds') else rd.FileType.PNG
    save.typeCast = rd.CompType.SInt if integer else rd.CompType.Depth
    save.mip = 0
    save.slice.sliceIndex = 0
    save.channelExtract = 0
    save.comp.blackPoint = 0.0
    save.comp.whitePoint = 1.0
    result = controller.SaveTexture(save, str(output / filename))
    assert result == rd.ResultCode.Succeeded, str(result)
    mapping = 'R32_SINT preserved' if filename.endswith('.dds') else (
        'SInt PNG conversion: entity 42 -> gray 42; clear -1 -> gray 255' if integer else
        'Depth [0, 1]: 0.5 -> gray 127; 1.0 -> gray 255')
    return {'file': filename, 'resource': str(resource), 'mapping': mapping,
            'export_result': str(result)}


def material_state(pipeline):
    reflection = pipeline.GetShaderReflection(rd.ShaderStage.Fragment)
    resources = []
    alpha_textures = []
    for used in pipeline.GetReadOnlyResources(rd.ShaderStage.Fragment):
        binding = reflection.readOnlyResources[used.access.index].fixedBindNumber
        texture = used.descriptor.resource
        resources.append({'binding': binding, 'resource': str(texture)})
        if binding in (3, 10):
            pixels = bytes(controller.GetTextureData(texture, subresource))
            channel = 3 if binding == 3 else 0
            values = list(pixels[channel::4])
            assert values == ([255, 64, 255, 255] if binding == 3 else [255, 255, 64, 255])
            alpha_textures.append({'binding': binding, 'resource': str(texture),
                                   'channel': 'A' if binding == 3 else 'R', 'values_u8': values})
    assert len(alpha_textures) == 2

    block = next(used for used in pipeline.GetConstantBlocks(rd.ShaderStage.Fragment)
                 if reflection.constantBlocks[used.access.index].fixedBindNumber == 2)
    data = bytes(controller.GetBufferData(block.descriptor.resource, block.descriptor.byteOffset, 64))
    values = {'base_color': list(struct.unpack_from('<4f', data, 0)),
              'metallic': struct.unpack_from('<f', data, 16)[0],
              'roughness': struct.unpack_from('<f', data, 20)[0],
              'ao': struct.unpack_from('<f', data, 24)[0],
              'use_base_color_map': struct.unpack_from('<i', data, 28)[0],
              'use_normal_map': struct.unpack_from('<i', data, 32)[0],
              'use_metallic_roughness_map': struct.unpack_from('<i', data, 36)[0],
              'use_ao_map': struct.unpack_from('<i', data, 40)[0],
              'entity_id': struct.unpack_from('<i', data, 44)[0],
              'use_alpha_map': struct.unpack_from('<i', data, 48)[0],
              'alpha_cutoff': struct.unpack_from('<f', data, 52)[0],
              'alpha_mode': struct.unpack_from('<i', data, 56)[0]}
    assert values['base_color'] == [1.0, 1.0, 1.0, 0.75]
    assert values['use_base_color_map'] == values['use_alpha_map'] == 1
    assert values['alpha_mode'] == 1 and values['alpha_cutoff'] == 0.5
    return {'read_only_bindings': resources, 'alpha_textures': alpha_textures,
            'material_buffer': {'binding': 2, 'resource': str(block.descriptor.resource),
                                'bytes': len(data), 'values': values}}


try:
    capture = rd.OpenCaptureFile()
    result = capture.OpenFile(str(output / report['source']), '', None)
    report['open_file'] = str(result)
    assert result == rd.ResultCode.Succeeded, str(result)
    options = rd.ReplayOptions()
    options.apiValidation = True
    result, controller = capture.OpenCapture(options, None)
    report['open_capture'] = str(result)
    assert result == rd.ResultCode.Succeeded, str(result)
    # 捕获中的驱动消息，与本次重新执行/读回时新增的消息分开保存。
    report['capture_messages'] = messages(controller.GetFrameInfo().debugMessages)
    report['replay_messages'] = []
    collect_messages('open_capture')
    actions = list(draws(controller.GetRootActions()))
    assert len(actions) == 2
    report['events'] = []
    all_depths = []

    for label, action in zip(('prepass', 'main'), actions):
        controller.SetFrameEvent(action.eventId, True)
        collect_messages('set_event_' + str(action.eventId))
        pipeline = controller.GetPipelineState()
        gl = controller.GetGLPipelineState()
        depth = pipeline.GetDepthTarget().resource
        entity = pipeline.GetOutputTargets()[1].resource
        blends = pipeline.GetColorBlends()[:3]
        masks = [blend.writeMask for blend in blends]
        assert gl.depthState.depthEnable and gl.depthState.depthWrites
        assert gl.depthState.depthFunction == rd.CompareFunction.LessEqual
        assert masks == ([0, 0, 15] if label == 'prepass' else [15, 15, 15])
        assert not any(blend.enabled for blend in blends)
        row = {'pass': label, 'event': action.eventId, 'index_count': action.numIndices,
               'depth_state': {'test': True, 'write': True, 'function': str(gl.depthState.depthFunction)},
               'color_write_masks': masks, 'blending': False,
               'shader_color_outputs': [x.regIndex for x in pipeline.GetShaderReflection(rd.ShaderStage.Fragment).outputSignature],
               'material': material_state(pipeline),
               'depth': save_texture(depth, label + '-depth.png'),
               'entity': save_texture(entity, label + '-entity.png', True),
               'entity_lossless': save_texture(entity, label + '-entity.dds', True), 'pixels': []}

        # 直接校验回放附件的全部原始值；PNG 只负责展示，不用于数值断言。
        depth_bytes = bytes(controller.GetTextureData(depth, subresource))
        entity_bytes = bytes(controller.GetTextureData(entity, subresource))
        depths = struct.unpack('<1024f', depth_bytes)
        ids = struct.unpack('<1024i', entity_bytes)
        for pixel, (d, entity_id) in enumerate(zip(depths, ids)):
            visible = pixel % 32 < 8 or pixel % 32 >= 24
            assert d == (0.5 if visible else 1.0), (label, pixel, d)
            assert entity_id == (42 if visible and label == 'main' else -1), (label, pixel, entity_id)
        all_depths.append(depth_bytes)
        row['whole_image'] = {'pixels_checked': 1024, 'visible_depth_pixels': depths.count(0.5),
                              'clear_depth_pixels': depths.count(1.0), 'entity_42_pixels': ids.count(42),
                              'entity_minus_1_pixels': ids.count(-1),
                              'depth_sha256': hashlib.sha256(depth_bytes).hexdigest(),
                              'entity_sha256': hashlib.sha256(entity_bytes).hexdigest()}
        # 覆盖左侧可见区、被裁剪区、右侧可见区。坐标来自回放 API 的左上角约定。
        for x, y in ((4, 16), (12, 16), (28, 16)):
            d = controller.PickPixel(depth, x, y, subresource, rd.CompType.Typeless).floatValue[0]
            entity_id = controller.PickPixel(entity, x, y, subresource, rd.CompType.Typeless).intValue[0]
            expected_depth = 1.0 if x == 12 else 0.5
            expected_id = 42 if label == 'main' and x != 12 else -1
            assert abs(d - expected_depth) < 1e-6, (label, x, d)
            assert entity_id == expected_id, (label, x, entity_id)
            row['pixels'].append({'x': x, 'y': y, 'depth': d, 'entity_id': entity_id})
        report['events'].append(row)
        collect_messages('inspect_event_' + str(action.eventId))
    assert all_depths[0] == all_depths[1]
    report['whole_image_checks'] = 'passed: 1024 pixels per event; depth attachments byte-identical'
    report['pixel_checks'] = 'passed'
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
    (output / 'replay-results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')

# --python 收到 SystemExit 后不会打开主窗口，避免与用户正在查看的实例混用。
sys.exit(0 if report['status'] == 'passed' else 1)
