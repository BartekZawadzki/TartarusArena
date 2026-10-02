"""mesh_catalog.py — writes Saved/mesh_catalog.tsv: every StaticMesh under the given roots with its bounding-box size
in metres and whether it has simple collision. Used to pick and fit map dressing. Prints CATALOG OK n=<count>.
"""
import os
import unreal

ROOTS = ["/Game/KiteDemo", "/Game/ParagonProps/Monolith/Ruins", "/Game/ParagonProps/Monolith/Rocks",
         "/Game/ParagonProps/Agora", "/Game/ParagonProps/Ground", "/Game/ParagonProps/Monolith/Dawn",
         "/Game/ParagonProps/Monolith/Dusk", "/Game/Lighting", "/Game/SampleMap"]
OUT = os.path.join(unreal.Paths.project_saved_dir(), "mesh_catalog.tsv")

reg = unreal.AssetRegistryHelpers.get_asset_registry()
rows = []
for root in ROOTS:
    for data in reg.get_assets_by_path(root, recursive=True):
        if str(data.asset_class_path.asset_name) != "StaticMesh":
            continue
        path = f"{data.package_name}.{data.asset_name}"
        mesh = unreal.load_asset(path)
        if not mesh:
            continue
        ext = mesh.get_bounds().box_extent
        body = mesh.get_editor_property("body_setup")
        simple = 0
        if body:
            agg = body.get_editor_property("agg_geom")
            simple = len(agg.get_editor_property("box_elems")) + len(agg.get_editor_property("convex_elems")) + \
                len(agg.get_editor_property("sphere_elems")) + len(agg.get_editor_property("sphyl_elems"))
        rows.append(f"{path}\t{ext.x * 0.02:.1f}\t{ext.y * 0.02:.1f}\t{ext.z * 0.02:.1f}\t{simple}")
with open(OUT, "w", encoding="utf-8") as f:
    f.write("path\tsize_x_m\tsize_y_m\tsize_z_m\tsimple_collision\n" + "\n".join(rows) + "\n")
print(f"CATALOG OK n={len(rows)} -> {OUT}")
