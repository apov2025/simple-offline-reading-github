#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""把小纸 Pico 的 3D 模型（GLB）转成模拟器外壳用的紧凑网格文件。

Convert the Read Pico 3D model (GLB) into the compact mesh the simulator shell loads.

用法 / Usage:
    python3 sim/tools/glb_to_mesh.py <device.glb> sim/assets/read_pico.mesh

模型来源 / Model source: apov catalog 的小纸 Pico 页面
(catalog/product/mindreset-read-pico/models/device.glb)，由小纸的 STEP 文件制作。

输出格式（小端）/ Output format (little endian):
    "RPM1", u32 材质数, u32 顶点数, u32 三角形数
    材质 / materials: 每个 char[16] 名字 + float[3] 基色（0..1）
    顶点 / vertices:  每个 float[3] 位置（毫米）+ float[3] 法线
    三角形 / triangles: 每个 u32[3] 顶点下标 + u32 材质下标
"""
import json
import struct
import sys

COMPONENT = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


def read_accessor(doc, blob, index):
    acc = doc["accessors"][index]
    view = doc["bufferViews"][acc["bufferView"]]
    fmt = COMPONENT[acc["componentType"]]
    width = WIDTH[acc["type"]]
    size = struct.calcsize(fmt) * width
    stride = view.get("byteStride", size)
    base = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    return [struct.unpack_from("<" + fmt * width, blob, base + i * stride) for i in range(acc["count"])]


def main(src, dst):
    data = open(src, "rb").read()
    magic, _version, _length = struct.unpack_from("<4sII", data, 0)
    if magic != b"glTF":
        sys.exit("not a GLB file")
    json_len, _ = struct.unpack_from("<I4s", data, 12)
    doc = json.loads(data[20:20 + json_len])
    blob = data[20 + json_len + 8:]

    materials = []
    for mat in doc["materials"]:
        color = mat.get("pbrMetallicRoughness", {}).get("baseColorFactor", [0.8, 0.8, 0.8, 1.0])
        materials.append((mat.get("name", "")[:15], color[:3]))

    vertices, triangles = [], []
    for node in doc["nodes"]:
        # 这个模型的节点都没有变换；有的话在这里拒绝，免得悄悄画错位置。
        # Nodes in this model carry no transform; refuse rather than silently misplace geometry.
        if any(key in node for key in ("matrix", "translation", "rotation", "scale")):
            sys.exit("node transforms are not supported: " + node.get("name", "?"))
        if "mesh" not in node:
            continue
        for prim in doc["meshes"][node["mesh"]]["primitives"]:
            if prim.get("mode", 4) != 4:
                continue
            positions = read_accessor(doc, blob, prim["attributes"]["POSITION"])
            normals = read_accessor(doc, blob, prim["attributes"]["NORMAL"])
            indices = [i[0] for i in read_accessor(doc, blob, prim["indices"])]
            base = len(vertices)
            for p, n in zip(positions, normals):
                vertices.append((p[0] * 1000.0, p[1] * 1000.0, p[2] * 1000.0, n[0], n[1], n[2]))
            for i in range(0, len(indices), 3):
                triangles.append((base + indices[i], base + indices[i + 1], base + indices[i + 2], prim["material"]))

    with open(dst, "wb") as out:
        out.write(struct.pack("<4sIII", b"RPM1", len(materials), len(vertices), len(triangles)))
        for name, color in materials:
            out.write(struct.pack("<16s3f", name.encode("ascii"), *color))
        for vertex in vertices:
            out.write(struct.pack("<6f", *vertex))
        for tri in triangles:
            out.write(struct.pack("<4I", *tri))
    print(f"{dst}: {len(materials)} materials, {len(vertices)} vertices, {len(triangles)} triangles")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
