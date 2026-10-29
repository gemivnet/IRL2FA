#!/usr/bin/env bash
# SPDX-License-Identifier: CC-BY-SA-4.0
# Build the IRL2FA case: regenerate board dimensions, export STLs + 3MF, render PNGs.
#
#   case/build.sh              # everything
#   case/build.sh --no-render  # STLs/3MF only (CI uses the full build)
#
# Needs docker (image openscad/openscad:latest, override with OPENSCAD_IMAGE) and a
# Python with PyYAML + trimesh (the repo .venv is used when present).
set -euo pipefail

CASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$CASE_DIR/.." && pwd)"
IMAGE="${OPENSCAD_IMAGE:-openscad/openscad:latest}"
RENDER=1
[[ "${1:-}" == "--no-render" ]] && RENDER=0

if [[ -n "${PYTHON:-}" ]]; then PY="$PYTHON"
elif [[ -x "$REPO/.venv/bin/python" ]]; then PY="$REPO/.venv/bin/python"
else PY=python3; fi

PARTS=(front back door cap)
STL_DIR="$CASE_DIR/stl"
IMG_DIR="$REPO/docs/images/case"
LOG_DIR="$(mktemp -d)"
trap 'rm -rf "$LOG_DIR"' EXIT
mkdir -p "$STL_DIR" "$CASE_DIR/3mf" "$IMG_DIR"

# All OpenSCAD runs happen in the container, as the calling user, with the repo at /repo.
oscad() {
    docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp \
        -v "$REPO":/repo -w /repo/case "$IMAGE" "$@"
}

echo "== board dimensions"
"$PY" "$CASE_DIR/tools/gen_dims.py"

BACKEND=()
if oscad openscad --help 2>&1 | grep -q -- '--backend'; then
    BACKEND=(--backend=manifold)
    echo "== using the manifold backend"
fi

echo "== exporting STLs"
pids=()
for p in "${PARTS[@]}"; do
    oscad openscad "${BACKEND[@]}" -D "part=\"$p\"" --export-format binstl \
        -o "stl/irl2fa-$p.stl" irl2fa_case.scad >"$LOG_DIR/$p.log" 2>&1 &
    pids+=($!)
done
fail=0
for i in "${!PARTS[@]}"; do
    if ! wait "${pids[$i]}"; then
        echo "!! ${PARTS[$i]} failed:"; cat "$LOG_DIR/${PARTS[$i]}.log"; fail=1
    fi
    if grep -qiE '^(WARNING|ERROR)' "$LOG_DIR/${PARTS[$i]}.log"; then
        echo "!! ${PARTS[$i]} produced warnings:"; grep -iE '^(WARNING|ERROR)' "$LOG_DIR/${PARTS[$i]}.log"; fail=1
    fi
done
[[ $fail == 0 ]] || exit 1
grep -h '^ECHO: "\(IRL2FA\|NOTE\)' "$LOG_DIR/front.log" | sed 's/^ECHO: //' || true

echo "== case info"
oscad sh -c "openscad -D 'part=\"info\"' -o /tmp/info.echo irl2fa_case.scad >/dev/null 2>&1; cat /tmp/info.echo" \
    | sed -n 's/^ECHO: "CASEINFO\(.*\)"$/\1/p' | sed 's/\\"/"/g' >"$STL_DIR/irl2fa-case-info.json"
"$PY" -c "import json,sys; json.load(open(sys.argv[1]))" "$STL_DIR/irl2fa-case-info.json"

echo "== 3MF"
"$PY" "$CASE_DIR/tools/make_3mf.py" "$STL_DIR" "$CASE_DIR/3mf/irl2fa-all.3mf"

if [[ $RENDER == 1 ]]; then
    echo "== renders"
    # name | part | camera (rot x,y,z) | extra -D
    renders=(
        "case-assembly|assembly|58,0,32|"
        "case-exploded|exploded|62,0,32|"
        "case-front|assembly|0,0,0|"
        "case-back|assembly|58,0,208|flip_view=true"
    )
    cmds=""
    for r in "${renders[@]}"; do
        IFS='|' read -r name part rot extra <<<"$r"
        d="-D from_stl=true -D part=\\\"$part\\\""
        [[ -n "$extra" ]] && d="$d -D $extra"
        cmds+="openscad $d --camera=0,0,0,$rot,0 --viewall --autocenter --projection=p"
        cmds+=" --imgsize=1600,1000 --colorscheme=Tomorrow -o /repo/docs/images/case/$name.png irl2fa_case.scad"
        cmds+=" >/tmp/$name.log 2>&1 || { cat /tmp/$name.log; exit 1; }; "
    done
    # Xvfb gives OpenSCAD's OpenGL preview a display inside the container.
    oscad sh -c "Xvfb :99 -screen 0 1600x1000x24 >/dev/null 2>&1 & sleep 1; export DISPLAY=:99; $cmds"
    "$PY" "$CASE_DIR/tools/postprocess_png.py" "$IMG_DIR"/case-*.png
fi

echo "== done"
ls -l "$STL_DIR" "$CASE_DIR/3mf"
