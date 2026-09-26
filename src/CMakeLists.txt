# Execute core target first so it can compile the shared Precompiled Header (PCH)
ADD_SUBDIRECTORY(core)

# Order and execute the remaining background processing modules
ADD_SUBDIRECTORY(engine)
ADD_SUBDIRECTORY(audiofileio)
ADD_SUBDIRECTORY(commands)
ADD_SUBDIRECTORY(plugins)
ADD_SUBDIRECTORY(sheetcanvas)
ADD_SUBDIRECTORY(traverso)
