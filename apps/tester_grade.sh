#!/bin/bash

set -o pipefail
#set -xv # debug

#
# Logging helpers
#
log() {
    echo -e "${*}"
}

inf() {
    log "Info: ${*}"
}
warning() {
    log "Warning: ${*}"
}
error() {
    log "Error: ${*}"
}
die() {
    error "${*}"
    exit 1
}

#
# Scoring helpers
#
``
TESTS_SCORE=0

select_line() {
	# 1: string
	# 2: line to select
	echo $(echo "${1}" | sed "${2}q;d")
}

fail() {
	# 1: got
	# 2: expected
    log "Fail: got '${1}' but expected '${2}'"
}

pass() {
	# got
    log "Pass: ${1}"
}

compare_lines() {
	TESTS_TOTAL=$(($TESTS_TOTAL + 1))
    # 1: output
	# 2: expected
    # 3: score (output)
	declare -a output_lines=("${!1}")
	declare -a expect_lines=("${!2}")
    local __score=$3
    local partial="0"
    local success="0"

    # Amount of partial credit for each correct output line
    local step=$(bc -l <<< "1.0 / ${#expect_lines[@]}")

    # Compare lines, two by two
	for i in ${!output_lines[*]}; do
		if [[ "${output_lines[${i}]}" =~ "${expect_lines[${i}]}" ]]; then
			pass "${output_lines[${i}]}"
            partial=$(bc <<< "${partial} + ${step}")
            success=1
		else
			fail "${output_lines[${i}]}" "${expect_lines[${i}]}" ]]
		fi
	done

    # Return final score
    TESTS_SCORE=$(($TESTS_SCORE + $success))
    eval ${__score}="'${partial}'"
}

#
# Generic function for running FS tests
#
run_test() {
    # These are global variables after the test has run so clear them out now
	unset STDOUT STDERR RET

    # Create temp files for getting stdout and stderr
    local outfile=$(mktemp)
    local errfile=$(mktemp)

    timeout 2 "${@}" >${outfile} 2>${errfile}

    # Get the return status, stdout and stderr of the test case
    RET="${?}"
    STDOUT=$(cat "${outfile}")
    STDERR=$(cat "${errfile}")

    # Deal with the possible timeout errors
    [[ ${RET} -eq 127 ]] && warning "Something is wrong (the executable probably doesn't exists)"
    [[ ${RET} -eq 124 ]] && warning "Command timed out..."

    # Clean up temp files
    rm -f "${outfile}"
    rm -f "${errfile}"
}

#
# Generic function for capturing output of non-test programs
#
run_tool() {
    # Create temp files for getting stdout and stderr
    local outfile=$(mktemp)
    local errfile=$(mktemp)

    timeout 2 "${@}" >${outfile} 2>${errfile}

    # Get the return status, stdout and stderr of the test case
    local ret="${?}"
    local stdout=$(cat "${outfile}")
    local stderr=$(cat "${errfile}")

    # Log the output
    [[ ! -z ${stdout} ]] && inf "${stdout}"
    [[ ! -z ${stderr} ]] && inf "${stderr}"

    # Deal with the possible timeout errors
    [[ ${ret} -eq 127 ]] && warning "Tool execution failed..."
    [[ ${ret} -eq 124 ]] && warning "Tool execution timed out..."

    # Clean up temp files
    rm -f "${outfile}"
    rm -f "${errfile}"
}

#
# Phase 1
#

# Info on empty disk
info() {
    log "\n--- Running ${FUNCNAME} ---"

    run_tool ./fs_make.x test.fs 100
    run_test ./test_fs.x info test.fs
    rm -f test.fs

    local line_array=()
    line_array+=("$(select_line "${STDOUT}" "1")")
    line_array+=("$(select_line "${STDOUT}" "2")")
    line_array+=("$(select_line "${STDOUT}" "3")")
    line_array+=("$(select_line "${STDOUT}" "4")")
    line_array+=("$(select_line "${STDOUT}" "5")")
    line_array+=("$(select_line "${STDOUT}" "6")")
    line_array+=("$(select_line "${STDOUT}" "7")")
    line_array+=("$(select_line "${STDOUT}" "8")")
    local corr_array=()
    corr_array+=("FS Info:")
    corr_array+=("total_blk_count=103")
    corr_array+=("fat_blk_count=1")
    corr_array+=("rdir_blk=2")
    corr_array+=("data_blk=3")
    corr_array+=("data_blk_count=100")
    corr_array+=("fat_free_ratio=99/100")
    corr_array+=("rdir_free_ratio=128/128")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}

# Info with files
info_full() {
    log "\n--- Running ${FUNCNAME} ---"

	run_tool ./fs_make.x test.fs 100
	run_tool dd if=/dev/urandom of=test-file-1 bs=2048 count=1
	run_tool dd if=/dev/urandom of=test-file-2 bs=2048 count=2
	run_tool dd if=/dev/urandom of=test-file-3 bs=2048 count=4
	run_tool ./fs_ref.x add test.fs test-file-1
	run_tool ./fs_ref.x add test.fs test-file-2
	run_tool ./fs_ref.x add test.fs test-file-3

	run_test ./test_fs.x info test.fs
	rm -f test-file-1 test-file-2 test-file-3 test.fs

	local line_array=()
	line_array+=("$(select_line "${STDOUT}" "7")")
	line_array+=("$(select_line "${STDOUT}" "8")")
	local corr_array=()
	corr_array+=("fat_free_ratio=95/100")
	corr_array+=("rdir_free_ratio=125/128")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}

#
# Phase 2
#

# make fs with fs_make.x, add empty file with test_fs.x, ls with fs_ref.x
create_simple() {
    log "\n--- Running ${FUNCNAME} ---"

	run_tool ./fs_make.x test.fs 10
	run_tool touch test-file-1
	run_tool timeout 2 ./test_fs.x add test.fs test-file-1
	run_test ./fs_ref.x ls test.fs
	rm -f test.fs test-file-1

	local line_array=()
	line_array+=("$(select_line "${STDOUT}" "2")")
	local corr_array=()
	corr_array+=("file: test-file-1, size: 0, data_blk: 65535")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}

#
# Phase 3 + 4
#

# read one block
read_block() {
    log "\n--- Running ${FUNCNAME} ---"

	run_tool ./fs_make.x test.fs 10
	run_tool ./fs_ref.x add test.fs test-file-1.txt
    
    run_test ./test_fs.x script test.fs scripts/read_block.script

	rm -f test.fs

	local line_array=()
	line_array+=("$(select_line "${STDOUT}" "3")")
	local corr_array=()
	corr_array+=("Read 4096 bytes from file. Compared 4096 correct.")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}

# read part of a block
read_partial_block() {
    log "\n--- Running ${FUNCNAME} ---"

	run_tool ./fs_make.x test.fs 10
	run_tool ./fs_ref.x add test.fs test-file-1.txt
    
    run_test ./test_fs.x script test.fs scripts/read_partial_block.script

	rm -f test.fs

	local line_array=()
	line_array+=("$(select_line "${STDOUT}" "3")")
	local corr_array=()
	corr_array+=("Read 40 bytes from file. Compared 40 correct.")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}

# read one and a half blocks
read_two_partial_blocks() {
    log "\n--- Running ${FUNCNAME} ---"

	run_tool ./fs_make.x test.fs 10
	run_tool ./fs_ref.x add test.fs test-file-1.txt

    run_test ./test_fs.x script test.fs scripts/read_two_partial_blocks.script

	rm -f test.fs

	local line_array=()
	line_array+=("$(select_line "${STDOUT}" "3")")
	local corr_array=()
	corr_array+=("Read 6144 bytes from file. Compared 6144 correct.")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}

# read six complete blocks
read_six_full_blocks() {
    log "\n--- Running ${FUNCNAME} ---"

	run_tool ./fs_make.x test.fs 10
	run_tool ./fs_ref.x add test.fs test-file-1.txt

    run_test ./test_fs.x script test.fs scripts/read_six_full_blocks.script

	rm -f test.fs

	local line_array=()
	line_array+=("$(select_line "${STDOUT}" "3")")
	local corr_array=()
	corr_array+=("Read 24576 bytes from file. Compared 24576 correct.")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}

# read seven and a half blocks
read_eight_partial_blocks() {
    log "\n--- Running ${FUNCNAME} ---"

	run_tool ./fs_make.x test.fs 10
	run_tool ./fs_ref.x add test.fs test-file-1.txt

    run_test ./test_fs.x script test.fs scripts/read_eight_partial_blocks.script

	rm -f test.fs

	local line_array=()
	line_array+=("$(select_line "${STDOUT}" "3")")
	local corr_array=()
	corr_array+=("Read 30000 bytes from file. Compared 30000 correct.")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}

# read all blocks
read_all_blocks() {
    log "\n--- Running ${FUNCNAME} ---"

	run_tool ./fs_make.x test.fs 10
	run_tool ./fs_ref.x add test.fs test-file-1.txt

    run_test ./test_fs.x script test.fs scripts/read_all_blocks.script

	rm -f test.fs

	local line_array=()
	line_array+=("$(select_line "${STDOUT}" "3")")
    local corr_array=()
	corr_array+=("Read 36864 bytes from file. Compared 36864 correct.")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}

# read blocks with request larger than file
read_blocks_overshoot() {
    log "\n--- Running ${FUNCNAME} ---"

	run_tool ./fs_make.x test.fs 10
    python3 -c "for i in range(6193): print('a', end='')" > test-file-2
	run_tool ./fs_ref.x add test.fs test-file-2

    run_test ./test_fs.x script test.fs scripts/read_blocks_overshoot.script

	rm -f test.fs test-file-2

	local line_array=()
	line_array+=("$(select_line "${STDOUT}" "3")")
    local corr_array=()
	corr_array+=("Read 6193 bytes from file. Compared 6193 correct.")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}

# write to the first few bytes of the first block
write_partial_block() {
    log "\n--- Running ${FUNCNAME} ---"

	run_tool ./fs_make.x test.fs 10
	run_tool ./fs_ref.x add test.fs test-file-1.txt

    run_test ./test_fs.x script test.fs scripts/write_partial_block.script

	rm -f test.fs

	local line_array=()
	line_array+=("$(select_line "${STDOUT}" "3")")
    local corr_array=()
	corr_array+=("Wrote 10 bytes to file.")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}

# write a block
write_block() {
    log "\n--- Running ${FUNCNAME} ---"

	run_tool ./fs_make.x test.fs 10
	run_tool ./fs_ref.x add test.fs test-file-1.txt

    run_test ./test_fs.x script test.fs scripts/write_block.script

	rm -f test.fs

	local line_array=()
	line_array+=("$(select_line "${STDOUT}" "3")")
    local corr_array=()
	corr_array+=("Wrote 4096 bytes to file.")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}

# write to the first few bytes of the first block
write_partial_block() {
    log "\n--- Running ${FUNCNAME} ---"

	run_tool ./fs_make.x test.fs 10
	run_tool ./fs_ref.x add test.fs test-file-1.txt

    run_test ./test_fs.x script test.fs scripts/write_partial_block.script

	rm -f test.fs

	local line_array=()
	line_array+=("$(select_line "${STDOUT}" "3")")
    local corr_array=()
	corr_array+=("Wrote 10 bytes to file.")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}

# write to one and a half blocks
write_two_partial_blocks() {
    log "\n--- Running ${FUNCNAME} ---"

	run_tool ./fs_make.x test.fs 10
	run_tool ./fs_ref.x add test.fs test-file-1.txt

    run_test ./test_fs.x script test.fs scripts/write_two_partial_blocks.script

	rm -f test.fs

	local line_array=()
	line_array+=("$(select_line "${STDOUT}" "3")")
    local corr_array=()
	corr_array+=("Wrote 5000 bytes to file.")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}

# write to one and a half blocks, write past file length (write up to end of FS)
write_past_file_1() {
    log "\n--- Running ${FUNCNAME} ---"

	run_tool ./fs_make.x test.fs 3
    python3 -c "for i in range(5000): print('a', end='')" > test-file-2
	run_tool ./fs_ref.x add test.fs test-file-2

    run_test ./test_fs.x script test.fs scripts/write_past_file_1.script

	rm -f test.fs test-file-2

	local line_array=()
	line_array+=("$(select_line "${STDOUT}" "3")")
    local corr_array=()
	corr_array+=("Wrote 8192 bytes to file.")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}

# write to one and a half blocks, write less than file length (allocate one more block)
write_block_2() {
    log "\n--- Running ${FUNCNAME} ---"

	run_tool ./fs_make.x test.fs 10
    python3 -c "for i in range(5000): print('a', end='')" > test-file-2
	run_tool ./fs_ref.x add test.fs test-file-2

    run_test ./test_fs.x script test.fs scripts/write_block_2.script

	rm -f test.fs test-file-2

	local line_array=()
	line_array+=("$(select_line "${STDOUT}" "3")")
    local corr_array=()
	corr_array+=("Wrote 10000 bytes to file.")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}

# write to partial of one block, write less than file length (allocate many blocks)
write_block_3() {
    log "\n--- Running ${FUNCNAME} ---"

	run_tool ./fs_make.x test.fs 10
    python3 -c "for i in range(1000): print('a', end='')" > test-file-2
	run_tool ./fs_ref.x add test.fs test-file-2

    run_test ./test_fs.x script test.fs scripts/write_block_3.script

	rm -f test.fs test-file-2

	local line_array=()
	line_array+=("$(select_line "${STDOUT}" "3")")
    local corr_array=()
	corr_array+=("Wrote 20000 bytes to file.")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}

# write to partial of one block, write more than file length (allocate only to file length)
write_past_file_2() {
    log "\n--- Running ${FUNCNAME} ---"

	run_tool ./fs_make.x test.fs 10
    python3 -c "for i in range(1000): print('a', end='')" > test-file-2
	run_tool ./fs_ref.x add test.fs test-file-2

    run_test ./test_fs.x script test.fs scripts/write_past_file_2.script

	rm -f test.fs test-file-2

	local line_array=()
	line_array+=("$(select_line "${STDOUT}" "3")")
    local corr_array=()
	corr_array+=("Wrote 36864 bytes to file.")

    local score
    compare_lines line_array[@] corr_array[@] score
    log "Score: ${score}"
}



#
# Run tests
#
run_tests() {
	# Phase 1
	info
	info_full
	#Phase 2
	create_simple
    # Phase 3 + 4
    read_block
    read_partial_block
    read_two_partial_blocks
    read_six_full_blocks
    read_eight_partial_blocks
    read_all_blocks
    read_blocks_overshoot
    write_partial_block
    write_block
    write_two_partial_blocks
    write_past_file_1
    write_block_2
    write_block_3
    write_past_file_2
}

make_fs() {
    # Compile
    make > /dev/null 2>&1 ||
        die "Compilation failed"

    local execs=("test_fs.x" "fs_make.x" "fs_ref.x")

    # Make sure executables were properly created
    local x
    for x in "${execs[@]}"; do
        if [[ ! -x "${x}" ]]; then
            die "Can't find executable ${x}"
        fi
    done
}

show_test_results() {
    echo -e "\nTotal tests passed: $TESTS_SCORE / $TESTS_TOTAL"
}

clean_fs() {
    # Delete all compiled files
    make clean > /dev/null 2>&1 ||
        die "Cleaning failed"
}

make_fs
run_tests
show_test_results
clean_fs
