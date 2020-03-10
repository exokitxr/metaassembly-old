{
  'targets': [
    {
      'target_name': 'exokit',
      'sources': [
        'module/module.cpp',
      ],
      'include_dirs': [
        "<!(node -e \"console.log(require.resolve('nan').slice(0, -16))\")",
      ],
      'library_dirs': [
        # "<!(node -e \"console.log(require.resolve('native-graphics-deps').slice(0, -9) + '/lib/windows/glew')\")",
      ],
      'libraries': [
        # 'opengl32.lib',
      ],
      'copies': [
      ],
      'defines': [
        'NOMINMAX',
        'OCULUSVR',
        'OPENVR',
        'LEAPMOTION',
        'WEBRTC_WIN',
      ],
    },
  ],
}
