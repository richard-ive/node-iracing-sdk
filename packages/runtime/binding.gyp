{
  "targets": [
    {
      "target_name": "irsdk_native",
      "include_dirs": [
        "irsdk_1_19"
      ],
      "defines": [
        "NAPI_VERSION=10"
      ],
      "cflags_cc": [
        "-std=c++17"
      ],
      "msvs_settings": {
        "VCCLCompilerTool": {
          "AdditionalOptions": ["/std:c++17"]
        }
      },
      "conditions": [
        [
          "OS=='win'",
          {
            "sources": [
              "src/addon.cpp",
              "irsdk_1_19/irsdk_client.cpp",
              "irsdk_1_19/irsdk_utils.cpp",
              "irsdk_1_19/yaml_parser.cpp"
            ]
          },
          {
            "sources": [
              "src/addon_stub.cpp"
            ]
          }
        ]
      ]
    }
  ]
}
