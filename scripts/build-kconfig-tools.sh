#!/bin/sh

set -eu

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <conf|mconf> <output> <hostcc>" >&2
    exit 1
fi

tool=$1
output=$2
hostcc=$3

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
kconfig_src="${script_dir}/kconfig"
scripts_include="${script_dir}/include"
out_dir=$(dirname "${output}")

case "${tool}" in
conf|mconf)
    ;;
*)
    echo "Unsupported tool '${tool}'" >&2
    exit 1
    ;;
esac

if [ ! -d "${kconfig_src}" ]; then
    echo "Missing vendored Kconfig sources: ${kconfig_src}" >&2
    exit 1
fi

mkdir -p "${out_dir}"

if [ -x "${kconfig_src}/${tool}" ]; then
    install -m 0755 "${kconfig_src}/${tool}" "${output}"
    exit 0
fi

if ! command -v bison >/dev/null 2>&1; then
    echo "Missing host tool: bison" >&2
    exit 1
fi

if ! command -v flex >/dev/null 2>&1; then
    echo "Missing host tool: flex" >&2
    exit 1
fi

base_flags="-I${kconfig_src} -I${scripts_include}"
parser_c="${out_dir}/parser.tab.c"
parser_h="${out_dir}/parser.tab.h"
lexer_c="${out_dir}/lexer.lex.c"

if [ ! -f "${parser_c}" ] || [ ! -f "${parser_h}" ] || [ "${kconfig_src}/parser.y" -nt "${parser_c}" ]; then
    bison -Wnone -o "${parser_c}" --defines="${parser_h}" "${kconfig_src}/parser.y"
fi

if [ ! -f "${lexer_c}" ] || [ "${kconfig_src}/lexer.l" -nt "${lexer_c}" ]; then
    flex -o "${lexer_c}" "${kconfig_src}/lexer.l"
fi

compile_object() {
    src=$1
    obj=$2
    shift 2
    "${hostcc}" ${common_flags} "$@" -c "${src}" -o "${obj}"
}

build_common_objects() {
    compile_object "${kconfig_src}/confdata.c" "${out_dir}/confdata.o"
    compile_object "${kconfig_src}/expr.c" "${out_dir}/expr.o"
    compile_object "${kconfig_src}/menu.c" "${out_dir}/menu.o"
    compile_object "${kconfig_src}/preprocess.c" "${out_dir}/preprocess.o"
    compile_object "${kconfig_src}/symbol.c" "${out_dir}/symbol.o"
    compile_object "${kconfig_src}/util.c" "${out_dir}/util.o"
    compile_object "${lexer_c}" "${out_dir}/lexer.lex.o"
    compile_object "${parser_c}" "${out_dir}/parser.tab.o" -DYYDEBUG=1
}

case "${tool}" in
conf)
    common_flags="${base_flags}"
    build_common_objects
    compile_object "${kconfig_src}/conf.c" "${out_dir}/conf.o"
    "${hostcc}" -o "${output}" \
        "${out_dir}/conf.o" \
        "${out_dir}/confdata.o" \
        "${out_dir}/expr.o" \
        "${out_dir}/lexer.lex.o" \
        "${out_dir}/menu.o" \
        "${out_dir}/parser.tab.o" \
        "${out_dir}/preprocess.o" \
        "${out_dir}/symbol.o" \
        "${out_dir}/util.o"
    ;;
mconf)
    cfg_cflags="${out_dir}/mconf.cflags"
    cfg_libs="${out_dir}/mconf.libs"

    hostpkg_config="${HOSTPKG_CONFIG:-pkg-config}"
    HOSTCC="${hostcc}" HOSTPKG_CONFIG="${hostpkg_config}" \
        "${kconfig_src}/mconf-cfg.sh" "${cfg_cflags}" "${cfg_libs}"

    common_flags="${base_flags} -I${kconfig_src}/lxdialog $(cat "${cfg_cflags}")"
    build_common_objects
    compile_object "${kconfig_src}/mconf.c" "${out_dir}/mconf.o"
    compile_object "${kconfig_src}/mnconf-common.c" "${out_dir}/mnconf-common.o"
    compile_object "${kconfig_src}/lxdialog/checklist.c" "${out_dir}/lxdialog-checklist.o"
    compile_object "${kconfig_src}/lxdialog/inputbox.c" "${out_dir}/lxdialog-inputbox.o"
    compile_object "${kconfig_src}/lxdialog/menubox.c" "${out_dir}/lxdialog-menubox.o"
    compile_object "${kconfig_src}/lxdialog/textbox.c" "${out_dir}/lxdialog-textbox.o"
    compile_object "${kconfig_src}/lxdialog/util.c" "${out_dir}/lxdialog-util.o"
    compile_object "${kconfig_src}/lxdialog/yesno.c" "${out_dir}/lxdialog-yesno.o"
    "${hostcc}" -o "${output}" \
        "${out_dir}/mconf.o" \
        "${out_dir}/mnconf-common.o" \
        "${out_dir}/confdata.o" \
        "${out_dir}/expr.o" \
        "${out_dir}/lexer.lex.o" \
        "${out_dir}/menu.o" \
        "${out_dir}/parser.tab.o" \
        "${out_dir}/preprocess.o" \
        "${out_dir}/symbol.o" \
        "${out_dir}/util.o" \
        "${out_dir}/lxdialog-checklist.o" \
        "${out_dir}/lxdialog-inputbox.o" \
        "${out_dir}/lxdialog-menubox.o" \
        "${out_dir}/lxdialog-textbox.o" \
        "${out_dir}/lxdialog-util.o" \
        "${out_dir}/lxdialog-yesno.o" \
        $(cat "${cfg_libs}")
    ;;
esac
