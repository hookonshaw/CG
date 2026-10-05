#!/bin/zsh
set -e
cd "${0:A:h}"
app_binary=./build/vulkan-starter-app.app/Contents/MacOS/vulkan-starter-app
rebuild=OFF
if [[ "${1:-}" == --build ]]; then
  rebuild=ON
  shift
fi
print "Лабораторная №1: подготовка запуска…"
if [[ -z "$VULKAN_SDK" ]]; then
  source "$HOME/VulkanSDK/1.4.321.0/setup-env.sh"
fi
if [[ "$rebuild" == OFF && -x "$app_binary" && -f build/CMakeCache.txt ]] && \
   grep -Fqx "CMAKE_HOME_DIRECTORY:INTERNAL=$PWD" build/CMakeCache.txt; then
  print "Запускаю приложение…"
  exec "$app_binary" "$@"
fi
print "Собираю проект…"
# CMake caches absolute paths; regenerate them after moving the project.
cmake_options=()
if [[ -f build/CMakeCache.txt ]] && ! grep -Fqx "CMAKE_HOME_DIRECTORY:INTERNAL=$PWD" build/CMakeCache.txt; then
  cmake_options+=(--fresh)
fi
# Reuse downloaded sources without running subbuilds with stale paths.
disconnected=ON
for dependency in glfw vk-bootstrap vulkanmemoryallocator imgui; do
  if [[ ! -d "build/_deps/${dependency}-src" ]]; then
    disconnected=OFF
  fi
done
cmake "${cmake_options[@]}" -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DFETCHCONTENT_FULLY_DISCONNECTED="$disconnected" \
  -DFETCHCONTENT_UPDATES_DISCONNECTED=ON
cmake --build build --parallel 4
exec "$app_binary" "$@"
