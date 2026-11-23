#include <blah_aseprite.h>
#include <blah_calc.h>
#include <blah_filesystem.h>

#define STBI_NO_STDIO
#define STBI_ONLY_ZLIB
#include "third_party/stb_image.h"

using namespace Blah;

Aseprite::Aseprite(const FilePath &path) {
	FileStream fs(path, FileMode::OpenRead);
	parse(fs);
}

Aseprite::Aseprite(Stream &stream) {
	parse(stream);
}

void Aseprite::parse(Stream &stream) {
	if (!stream.is_readable()) {
		BLAH_ASSERT(false, "Stream is not readable");
		return;
	}

	int frame_count = 0;

	// header
	{
		// filesize
		stream.read_u32(Endian::Little);

		// magic number
		auto magic = stream.read_u16(Endian::Little);
		if (magic != 0xA5E0) {
			BLAH_ASSERT(false, "File is not a valid Aseprite file");
			return;
		}

		// main info
		frame_count = stream.read_u16(Endian::Little);
		width		= stream.read_u16(Endian::Little);
		height		= stream.read_u16(Endian::Little);
		mode =
			static_cast<Aseprite::Modes>(stream.read_u16(Endian::Little) / 8);

		// don't care about other info
		stream.read_u32(Endian::Little); // Flags
		stream.read_u16(Endian::Little); // Speed (deprecated)
		stream.read_u32(Endian::Little); // Should be 0
		stream.read_u32(Endian::Little); // Should be 0
		m_transparent_index = stream.read_u8(Endian::Little);
		stream.seek(stream.position() + 3); // Ignore these bytes
		stream.read_u16(
			Endian::Little); // Number of colors (0 means 256 for old sprites)
		stream.read_i8(Endian::Little);		 // Pixel width
		stream.read_i8(Endian::Little);		 // Pixel height
		stream.seek(stream.position() + 92); // For Future
	}

	frames.resize(frame_count);
	if (mode == Modes::Indexed)
		palette.resize(256);

	// frames
	for (int i = 0; i < frame_count; i++) {
		auto		 frameStart = stream.position();
		auto		 frameEnd	= frameStart + stream.read_u32(Endian::Little);
		unsigned int chunks		= 0;

		// frame header
		{
			auto magic = stream.read_u16(Endian::Little); // magic number
			if (magic != 0xF1FA) {
				BLAH_ASSERT(false, "File is not a valid Aseprite file");
				return;
			}

			auto old_chunk_count = stream.read_u16(Endian::Little);
			frames[i].duration	 = stream.read_u16(Endian::Little);
			stream.seek(stream.position() + 2); // for future
			auto new_chunk_count = stream.read_u32(Endian::Little);

			if (old_chunk_count == 0xFFFF)
				chunks = new_chunk_count;
			else
				chunks = old_chunk_count;
		}

		// make frame image
		frames[i].image = Image(width, height);

		// Modern palette chunks take precedence over legacy chunks, even when
		// the legacy chunk occurs later in the frame.
		const auto chunksStart		= stream.position();
		bool	   hasModernPalette = false;
		for (unsigned int j = 0; j < chunks; ++j) {
			const auto start = stream.position();
			const auto size	 = stream.read_u32(Endian::Little);
			if (stream.read_u16(Endian::Little) == (u16) Chunks::Palette)
				hasModernPalette = true;
			stream.seek(start + size);
		}
		stream.seek(chunksStart);

		// frame chunks
		for (unsigned int j = 0; j < chunks; j++) {
			auto chunkStart = stream.position();
			auto chunkEnd	= chunkStart + stream.read_u32(Endian::Little);
			auto chunkType =
				static_cast<Chunks>(stream.read_u16(Endian::Little));

			switch (chunkType) {
				case Chunks::Layer:
					parse_layer(stream, i);
					break;
				case Chunks::Cel:
					parse_cel(stream, i, chunkEnd);
					break;
				case Chunks::OldPaletteA:
				case Chunks::OldPaletteB:
					if (!hasModernPalette)
						parse_old_palette(stream,
										  chunkType == Chunks::OldPaletteB);
					break;
				case Chunks::Palette:
					parse_palette(stream, i);
					break;
				case Chunks::UserData:
					parse_user_data(stream, i);
					break;
				case Chunks::FrameTags:
					parse_tag(stream, i);
					break;
				case Chunks::Slice:
					parse_slice(stream, i);
					break;
				default:
					break;
			}

			stream.seek(chunkEnd);
		}

		// Resolve indexed pixels only after all palette updates are known.
		for (auto &cel: frames[i].cels) {
			if (mode == Modes::Indexed && cel.linked_frame_index < 0) {
				for (int pixel = 0; pixel < cel.indices.size(); ++pixel)
					cel.image.pixels[pixel] = indexed_color(
						cel.indices[pixel], layers[cel.layer_index]);
			}
			if (layers[cel.layer_index].visible)
				render_cel(&cel, &frames[i]);
		}
		stream.seek(frameEnd);
	}
}

void Aseprite::parse_layer(Stream &stream, int frame) {
	layers.emplace_back();

	auto &layer	  = layers.back();
	layer.flag	  = static_cast<LayerFlags>(stream.read_u16(Endian::Little));
	layer.visible = ((int) layer.flag & (int) LayerFlags::Visible) ==
					(int) LayerFlags::Visible;
	layer.type	  = static_cast<LayerTypes>(stream.read_u16(Endian::Little));
	layer.child_level = stream.read_u16(Endian::Little);
	stream.read_u16(Endian::Little); // width
	stream.read_u16(Endian::Little); // height
	layer.blendmode = stream.read_u16(Endian::Little);
	layer.alpha		= stream.read_u8(Endian::Little);
	stream.seek(stream.position() + 3); // for future

	layer.name.append('\0', stream.read_u16(Endian::Little));
	stream.read(layer.name.cstr(), layer.name.length());

	layer.userdata.color = 0xffffff;
	layer.userdata.text	 = "";
	m_last_userdata		 = &(layer.userdata);
}

void Aseprite::parse_cel(Stream &stream, int frameIndex, size_t maxPosition) {
	Frame &frame = frames[frameIndex];

	frame.cels.emplace_back();
	auto &cel			   = frame.cels.back();
	cel.layer_index		   = stream.read_u16(Endian::Little);
	cel.x				   = stream.read_u16(Endian::Little);
	cel.y				   = stream.read_u16(Endian::Little);
	cel.alpha			   = stream.read_u8(Endian::Little);
	cel.linked_frame_index = -1;

	auto celType = stream.read_u16(Endian::Little);
	stream.seek(stream.position() + 7);

	// RAW or DEFLATE
	if (celType == 0 || celType == 2) {
		auto width	= stream.read_u16(Endian::Little);
		auto height = stream.read_u16(Endian::Little);
		auto count	= width * height * (int) mode;

		cel.image = Image(width, height);

		// RAW
		if (celType == 0) {
			stream.read(cel.image.pixels, count);
		}
		// DEFLATE (zlib)
		else {
			// this could be optimized to use a buffer on the stack if we only
			// read set chunks at a time stbi's zlib doesn't have that
			// functionality though
			auto size = maxPosition - stream.position();
			if (size > INT32_MAX)
				size = INT32_MAX;

			char *buffer = new char[size];
			stream.read(buffer, size);

			int olen = width * height * sizeof(Color);
			int res	 = stbi_zlib_decode_buffer((char *) cel.image.pixels, olen,
											   buffer, (int) size);

			delete[] buffer;

			if (res != count) {
				BLAH_ASSERT(false, "Unable to parse Aseprite file");
				return;
			}
		}

		// convert to pixels
		// note: we work in-place to save having to store stuff in a buffer
		if (mode == Modes::Grayscale) {
			auto src = (unsigned char *) cel.image.pixels;
			auto dst = cel.image.pixels;
			for (int d = width * height - 1, s = (width * height - 1) * 2;
				 d >= 0; d--, s -= 2)
				dst[d] = Color(src[s], src[s], src[s], src[s + 1]);
		} else if (mode == Modes::Indexed) {
			const auto src = (const u8 *) cel.image.pixels;
			cel.indices.resize(width * height);
			for (int i = 0; i < width * height; ++i)
				cel.indices[i] = src[i];
		}

	}
	// REFERENCE
	// this cel directly references a previous cel
	else if (celType == 1) {
		cel.linked_frame_index = stream.read_u16(Endian::Little);
	}

	cel.userdata.color = 0xffffff;
	cel.userdata.text  = "";
	m_last_userdata	   = &(cel.userdata);
}

void Aseprite::parse_palette(Stream &stream, int frame) {
	auto size  = stream.read_u32(Endian::Little);
	auto start = stream.read_u32(Endian::Little);
	auto end   = stream.read_u32(Endian::Little);
	stream.seek(stream.position() + 8);

	if (size > INT32_MAX || start > end || end >= size) {
		BLAH_ASSERT(false, "Invalid Aseprite palette range");
		return;
	}
	palette.resize(size);

	for (int p = 0, len = static_cast<int>(end - start) + 1; p < len; p++) {
		auto	   hasName = stream.read_u16(Endian::Little);
		const auto r	   = stream.read_u8();
		const auto g	   = stream.read_u8();
		const auto b	   = stream.read_u8();
		const auto a	   = stream.read_u8();
		palette[start + p] = Color(r, g, b, a);

		if (hasName & 1) {
			int len = stream.read_u16(Endian::Little);
			stream.seek(stream.position() + len);
		}
	}
}

void Aseprite::parse_old_palette(Stream &stream, bool six_bit) {
	if (palette.size() < 256)
		palette.resize(256);
	const auto packets = stream.read_u16(Endian::Little);
	int		   index   = 0;
	for (int packet = 0; packet < packets; ++packet) {
		index += stream.read_u8();
		int count = stream.read_u8();
		if (count == 0)
			count = 256;
		if (index + count > 256) {
			BLAH_ASSERT(false, "Invalid Aseprite legacy palette range");
			return;
		}
		for (int i = 0; i < count; ++i) {
			auto r = stream.read_u8();
			auto g = stream.read_u8();
			auto b = stream.read_u8();
			if (six_bit) {
				r = (r << 2) | (r >> 4);
				g = (g << 2) | (g >> 4);
				b = (b << 2) | (b >> 4);
			}
			palette[index++] = Color(r, g, b);
		}
	}
}

Color Aseprite::indexed_color(u8 index, const Layer &layer) const {
	if ((index == m_transparent_index &&
		 !((int) layer.flag & (int) LayerFlags::Background)) ||
		index >= palette.size())
		return Color::transparent;
	return palette[index];
}

void Aseprite::parse_user_data(Stream &stream, int frame) {
	if (m_last_userdata != nullptr) {
		auto flags = stream.read_u32(Endian::Little);

		// has text
		if (flags & (1 << 0)) {
			m_last_userdata->text.append('\0', stream.read_u16(Endian::Little));
			stream.read(m_last_userdata->text.cstr(),
						m_last_userdata->text.length());
		}

		// has color
		if (flags & (1 << 1))
			m_last_userdata->color = stream.read_u32(Endian::Little);
	}
}

void Aseprite::parse_tag(Stream &stream, int frame) {
	auto count = stream.read_u16(Endian::Little);
	stream.seek(stream.position() + 8);

	for (int t = 0; t < count; t++) {
		Tag tag;
		tag.from  = stream.read_u16(Endian::Little);
		tag.to	  = stream.read_u16(Endian::Little);
		tag.loops = static_cast<LoopDirections>(stream.read_i8(Endian::Little));

		stream.seek(stream.position() + 8);
		tag.color = Color(stream.read_i8(), stream.read_i8(),
						  stream.read_i8(Endian::Little), 255);
		stream.seek(stream.position() + 1);

		tag.name.append('\0', stream.read_u16(Endian::Little));
		stream.read(tag.name.cstr(), tag.name.length());

		tags.push_back(tag);
	}
}

void Aseprite::parse_slice(Stream &stream, int frame) {
	int count = stream.read_u32(Endian::Little);
	int flags = stream.read_u32(Endian::Little);
	stream.read_u32(Endian::Little); // reserved

	String name;
	name.append('\0', stream.read_u16(Endian::Little));
	stream.read(name.cstr(), name.length());

	for (int s = 0; s < count; s++) {
		slices.emplace_back();

		auto &slice	   = slices.back();
		slice.name	   = name;
		slice.frame	   = stream.read_u32(Endian::Little);
		slice.origin.x = stream.read_i32(Endian::Little);
		slice.origin.y = stream.read_i32(Endian::Little);
		slice.width	   = stream.read_u32(Endian::Little);
		slice.height   = stream.read_u32(Endian::Little);

		// 9 slice (ignored atm)
		if (flags & (1 << 0)) {
			stream.read_i32(Endian::Little);
			stream.read_i32(Endian::Little);
			stream.read_u32(Endian::Little);
			stream.read_u32(Endian::Little);
		}

		// pivot point
		slice.has_pivot = false;
		if (flags & (1 << 1)) {
			slice.has_pivot = true;
			slice.pivot.x	= stream.read_u32(Endian::Little);
			slice.pivot.y	= stream.read_u32(Endian::Little);
		}

		slice.userdata.color = 0xffffff;
		slice.userdata.text	 = "";
		m_last_userdata		 = &(slice.userdata);
	}
}

#define MUL_UN8(a, b, t)                                                       \
	((t) = (a) * (u16) (b) + 0x80, ((((t) >> 8) + (t)) >> 8))

void Aseprite::render_cel(Cel *cel, Frame *frame) {
	Layer &layer = layers[cel->layer_index];

	const auto srcX	 = cel->x;
	const auto srcY	 = cel->y;
	const auto alpha = cel->alpha;
	while (cel->linked_frame_index >= 0) {
		auto &frame = frames[cel->linked_frame_index];
		for (auto &it: frame.cels)
			if (it.layer_index == cel->layer_index) {
				cel = &it;
				break;
			}
	}

	int			  t;
	unsigned char opacity = MUL_UN8(alpha, layer.alpha, t);
	if (opacity <= 0)
		return;

	auto src  = cel->image.pixels;
	auto srcW = cel->image.width;
	auto srcH = cel->image.height;
	auto dst  = frame->image.pixels;
	auto dstW = frame->image.width;
	auto dstH = frame->image.height;

	// blit pixels
	int left   = Calc::max(0, srcX);
	int right  = Calc::min(dstW, srcX + srcW);
	int top	   = Calc::max(0, srcY);
	int bottom = Calc::min(dstH, srcY + srcH);

	if (layer.blendmode == 0) {
		for (int dx = left, sx = -Calc::min(srcX, 0); dx < right; dx++, sx++) {
			for (int dy = top, sy = -Calc::min(srcY, 0); dy < bottom;
				 dy++, sy++) {
				const auto	pixel = sx + sy * srcW;
				const Color color =
					mode == Modes::Indexed
						? indexed_color(cel->indices[pixel], layer)
						: src[pixel];
				const Color *srcColor = &color;
				Color		*dstColor = (dst + dx + dy * dstW);

				if (srcColor->a != 0) {
					auto sa = MUL_UN8(srcColor->a, opacity, t);
					auto ra = dstColor->a + sa - MUL_UN8(dstColor->a, sa, t);

					dstColor->r =
						(unsigned char) (dstColor->r +
										 (srcColor->r - dstColor->r) * sa / ra);
					dstColor->g =
						(unsigned char) (dstColor->g +
										 (srcColor->g - dstColor->g) * sa / ra);
					dstColor->b =
						(unsigned char) (dstColor->b +
										 (srcColor->b - dstColor->b) * sa / ra);
					dstColor->a = (unsigned char) ra;
				}
			}
		}
	} else {
		BLAH_ASSERT(false, "Aseprite blendmodes aren't implemented");
	}
}
