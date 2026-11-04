# Flat-binary guest payloads for the early roadmap stages: assembled with the
# system GNU toolchain and linked to raw binaries loaded by --payload.

payload_ld = find_program('ld', required: get_option('guest_payloads'))

if payload_ld.found()
  payload_sources = {
    'realmode_hello': 'payloads/realmode_hello.S',
    'longmode_hello': 'payloads/longmode_hello.S',
  }

  guest_payloads = {}
  foreach name, src : payload_sources
    obj = static_library('payload_' + name, src,
      c_args: ['-ffreestanding', '-nostdlib'],
      pic: false,
    )
    guest_payloads += {name: custom_target('payload-' + name,
      input: obj.extract_all_objects(recursive: false),
      output: name + '.bin',
      command: [payload_ld, '-T', files('payloads/payload.ld'), '--oformat', 'binary',
                '-o', '@OUTPUT@', '@INPUT@'],
      build_by_default: true,
    )}
  endforeach
endif
