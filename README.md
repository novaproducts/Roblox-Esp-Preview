# 3D ESP Preview Renderer

3D avatar rendering for roblox shitslpoits overlays. software rasterizer that draws textured models in imgui.

## what it does

- parses OBJ/MTL files (gzip compressed)
- async texture decoding with worker threads
- renders 3D models with lighting and textures
- displays the 3d 
- auto-rotation, depth sorting, bilinear filtering

## how it works

```
user_id → fetch avatar → parse obj/mtl → load textures → render with lighting
```

## files

- `obj_parser.cpp/hpp` - parse mesh geometry
- `mtl_parser.cpp/hpp` - parse materials/textures  
- `texture_cache.cpp/hpp` - async texture decoder with thread pool

## dependencies

- ImGui
- stb_image
- C++17

## usage

```cpp
// parse model
c_obj_model model;
parse_obj(avatar->obj_data, model);
parse_mtl(avatar->mtl_data, model, avatar->texture_hashes);

// request textures
c_texture_cache::get().request_texture(user_id, tex_index, data, true);

// render loop handles rest
```

## performance

- multi-threaded texture decoding
- ~60fps for <10k triangle models
- 512MB texture cache limit

## limitations

- no GPU acceleration
- no z-buffer (uses painter's algorithm)
- single-threaded rasterization

## links

https://en.wikipedia.org/wiki/Wavefront_.obj_file
https://codeplea.com/triangular-interpolation
https://en.wikipedia.org/wiki/Painter%27s_algorithm

---

cpu-based renderer for textured 3D models. parses obj/mtl, caches textures, rasterizes pixel-by-pixel.
