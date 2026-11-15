import ast
import os

def analyze_test_file():
    path = os.path.join("tests", "test_wp4_multiphysics_hexcore.py")
    with open(path, "r", encoding="utf-8") as f:
        tree = ast.parse(f.read(), filename=path)
    
    top_level_defs = []
    classes = {}
    imports = []
    
    for node in tree.body:
        if isinstance(node, (ast.Import, ast.ImportFrom)):
            imports.append(ast.unparse(node))
        elif isinstance(node, ast.FunctionDef):
            top_level_defs.append(node.name)
        elif isinstance(node, ast.ClassDef):
            methods = [n.name for n in node.body if isinstance(n, ast.FunctionDef)]
            classes[node.name] = methods
            
    prohibited_duplicates = [
        "compute_amips_energy",
        "compute_wsrd_weights",
        "compute_asme_vv20_gci",
        "compute_farfield_bounding_box"
    ]
    
    found_duplicates = [d for d in prohibited_duplicates if d in top_level_defs]
    
    return {
        "top_level_defs": top_level_defs,
        "classes": classes,
        "imports": imports,
        "found_duplicates": found_duplicates
    }

if __name__ == "__main__":
    res = analyze_test_file()
    print("Found prohibited duplicates:", res["found_duplicates"])
    print("Top level defs:", res["top_level_defs"])
    print("Classes found:")
    for cls, methods in res["classes"].items():
        print(f"  {cls}: {len(methods)} methods")
