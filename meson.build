project(
  'armu',
  ['c'],
  default_options: ['warning_level=1', 'buildtype=debugoptimized'],
  meson_version: '>=0.60.3',
)

disarm = subproject('disarm').get_variable('disarm64')
dynasm = subproject('dynasm').get_variable('dynasm_dep')
minilua = subproject('dynasm').get_variable('minilua')
dynasm_lua = subproject('dynasm').get_variable('dynasm_lua')

cpu_x64 = custom_target(
  'cpu_x64.gen.c',
  output: ['cpu_x64.gen.c'],
  input: files('cpu_x64.c'),
  command: [minilua, dynasm_lua, '-o', '@OUTPUT@', '@INPUT@'],
)

fib_compiler = find_program('aarch64-linux-gnu-gcc', required: false)
fib_target = []

if fib_compiler.found()
  fib_inputs = files('fibonacci.c', 'fibonacci_start.S')
  fib_target = custom_target(
    'fibonacci_bare',
    input: fib_inputs,
    output: 'fibonacci_bare',
    command: [
      fib_compiler, '-Os',
      '-fno-stack-protector',
      '-fomit-frame-pointer',
      '-fno-asynchronous-unwind-tables',
      '-nostdlib',
      '-static',
      '-Wl,--nmagic',
      '-Wl,--build-id=none',
      '-Wl,-Ttext=0x400000',
      '@INPUT@',
      '-o', '@OUTPUT@',
    ],
    build_by_default: true,
  )
  add_project_arguments('-DFIB_ELF_AVAILABLE=1', language: 'c')
  add_project_arguments('-DFIB_ELF_PATH="fibonacci_bare"', language: 'c')
  add_project_arguments('-DARMU_AARCH64_COMPILER="@0@"'.format(fib_compiler.full_path()), language: 'c')
  add_project_arguments('-DARMU_START_STUB_PATH="@0@/fibonacci_start.S"'.format(meson.current_source_dir()), language: 'c')
else
  add_project_arguments('-DFIB_ELF_AVAILABLE=0', language: 'c')
endif

srcs = files('armu.c', 'elf_loader.c', 'main.c', 'tests.c')

libarmu = executable(
  'armu',
  sources: srcs,
  dependencies: [disarm.as_link_whole(), dynasm],
  install: true,
)
