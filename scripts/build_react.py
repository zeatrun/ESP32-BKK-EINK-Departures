Import("env")
import subprocess
import os
import sys

def build_react_app(*args, **kwargs):
    project_dir = env.subst("$PROJECT_DIR")
    config_ui_dir = os.path.join(project_dir, "config_ui")

    if not os.path.isdir(config_ui_dir):
        print("⚠  config_ui/ not found, skipping React build")
        return

    # Install npm dependencies if node_modules is missing
    node_modules = os.path.join(config_ui_dir, "node_modules")
    if not os.path.isdir(node_modules):
        print("📦 Installing npm dependencies...")
        result = subprocess.run(
            ["npm", "install"],
            cwd=config_ui_dir,
            shell=True,
        )
        if result.returncode != 0:
            print("❌ npm install failed — skipping React build")
            return

    print("⚛  Building React config UI...")
    result = subprocess.run(
        ["npm", "run", "build"],
        cwd=config_ui_dir,
        shell=True,
    )

    if result.returncode == 0:
        print("✅ React build successful → data/config-app/")
    else:
        print("❌ React build FAILED — filesystem will not contain the config UI")
        sys.exit(1)

env.AddPreAction("buildfs", build_react_app)
