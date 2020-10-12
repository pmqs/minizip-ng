///////////////////////////////////////////////////////////////////////////////
//
/// \file       filter_decoder.c
/// \brief      Filter ID mapping to filter-specific functions
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "filter_encoder.h"
#include "lzma_encoder.h"
#ifdef HAVE_DECODER_LZMA2
#include "lzma2_encoder.h"
#endif
#if defined(HAVE_DECODER_X86) || \
    defined(HAVE_DECODER_POWERPC) || \
    defined(HAVE_DECODER_IA64) || \
    defined(HAVE_DECODER_ARM) || \
    defined(HAVE_DECODER_ARMTHUMB) || \
    defined(HAVE_DECODER_SPARC)
#include "simple_encoder.h"
#endif
#ifdef HAVE_DECODER_DELTA
#include "delta_encoder.h"
#endif


typedef struct {
	/// Filter ID
	lzma_vli id;

	/// Initializes the filter encoder and calls lzma_next_filter_init()
	/// for filters + 1.
	lzma_init_function init;

	/// Calculates memory usage of the encoder. If the options are
	/// invalid, UINT64_MAX is returned.
	uint64_t (*memusage)(const void *options);

	/// Calculates the recommended Uncompressed Size for .xz Blocks to
	/// which the input data can be split to make multithreaded
	/// encoding possible. If this is NULL, it is assumed that
	/// the encoder is fast enough with single thread.
	uint64_t (*block_size)(const void *options);

	/// Tells the size of the Filter Properties field. If options are
	/// invalid, UINT32_MAX is returned. If this is NULL, props_size_fixed
	/// is used.
	lzma_ret (*props_size_get)(uint32_t *size, const void *options);
	uint32_t props_size_fixed;

	/// Encodes Filter Properties.
	///
	/// \return     - LZMA_OK: Properties encoded successfully.
	///             - LZMA_OPTIONS_ERROR: Unsupported options
	///             - LZMA_PROG_ERROR: Invalid options or not enough
	///               output space
	lzma_ret (*props_encode)(const void *options, uint8_t *out);

} lzma_filter_encoder;


static const lzma_filter_encoder encoders[] = {
#ifdef HAVE_ENCODER_LZMA1
	{
		.id = LZMA_FILTER_LZMA1,
		.init = &lzma_lzma_encoder_init,
		.memusage = &lzma_lzma_encoder_memusage,
		.block_size = NULL, // FIXME
		.props_size_get = NULL,
		.props_size_fixed = 5,
		.props_encode = &lzma_lzma_props_encode,
	},
#endif
#ifdef HAVE_ENCODER_LZMA2
	{
		.id = LZMA_FILTER_LZMA2,
		.init = &lzma_lzma2_encoder_init,
		.memusage = &lzma_lzma2_encoder_memusage,
		.block_size = &lzma_lzma2_block_size, // FIXME
		.props_size_get = NULL,
		.props_size_fixed = 1,
		.props_encode = &lzma_lzma2_props_encode,
	},
#endif
#ifdef HAVE_ENCODER_X86
	{
		.id = LZMA_FILTER_X86,
		.init = &lzma_simple_x86_encoder_init,
		.memusage = NULL,
		.block_size = NULL,
		.props_size_get = &lzma_simple_props_size,
		.props_encode = &lzma_simple_props_encode,
	},
#endif
#ifdef HAVE_ENCODER_POWERPC
	{
		.id = LZMA_FILTER_POWERPC,
		.init = &lzma_simple_powerpc_encoder_init,
		.memusage = NULL,
		.block_size = NULL,
		.props_size_get = &lzma_simple_props_size,
		.props_encode = &lzma_simple_props_encode,
	},
#endif
#ifdef HAVE_ENCODER_IA64
	{
		.id = LZMA_FILTER_IA64,
		.init = &lzma_simple_ia64_encoder_init,
		.memusage = NULL,
		.block_size = NULL,
		.props_size_get = &lzma_simple_props_size,
		.props_encode = &lzma_simple_props_encode,
	},
#endif
#ifdef HAVE_ENCODER_ARM
	{
		.id = LZMA_FILTER_ARM,
		.init = &lzma_simple_arm_encoder_init,
		.memusage = NULL,
		.block_size = NULL,
		.props_size_get = &lzma_simple_props_size,
		.props_encode = &lzma_simple_props_encode,
	},
#endif
#ifdef HAVE_ENCODER_ARMTHUMB
	{
		.id = LZMA_FILTER_ARMTHUMB,
		.init = &lzma_simple_armthumb_encoder_init,
		.memusage = NULL,
		.block_size = NULL,
		.props_size_get = &lzma_simple_props_size,
		.props_encode = &lzma_simple_props_encode,
	},
#endif
#ifdef HAVE_ENCODER_SPARC
	{
		.id = LZMA_FILTER_SPARC,
		.init = &lzma_simple_sparc_encoder_init,
		.memusage = NULL,
		.block_size = NULL,
		.props_size_get = &lzma_simple_props_size,
		.props_encode = &lzma_simple_props_encode,
	},
#endif
#ifdef HAVE_ENCODER_DELTA
	{
		.id = LZMA_FILTER_DELTA,
		.init = &lzma_delta_encoder_init,
		.memusage = &lzma_delta_coder_memusage,
		.block_size = NULL,
		.props_size_get = NULL,
		.props_size_fixed = 1,
		.props_encode = &lzma_delta_props_encode,
	},
#endif
};


static const lzma_filter_encoder *
encoder_find(lzma_vli id)
{
	for (size_t i = 0; i < ARRAY_SIZE(encoders); ++i)
		if (encoders[i].id == id)
			return encoders + i;

	return NULL;
}


extern LZMA_API(lzma_bool)
lzma_filter_encoder_is_supported(lzma_vli id)
{
	return encoder_find(id) != NULL;
}


extern uint64_t
lzma_mt_block_size(const lzma_filter *filters)
{
	uint64_t max = 0;

	for (size_t i = 0; filters[i].id != LZMA_VLI_UNKNOWN; ++i) {
		const lzma_filter_encoder *const fe
				= encoder_find(filters[i].id);
		if (fe->block_size != NULL) {
			const uint64_t size
					= fe->block_size(filters[i].options);
			if (size == 0)
				return 0;

			if (size > max)
				max = size;
		}
	}

	return max;
}


extern LZMA_API(lzma_ret)
lzma_properties_size(uint32_t *size, const lzma_filter *filter)
{
	const lzma_filter_encoder *const fe = encoder_find(filter->id);
	if (fe == NULL) {
		// Unknown filter - if the Filter ID is a proper VLI,
		// return LZMA_OPTIONS_ERROR instead of LZMA_PROG_ERROR,
		// because it's possible that we just don't have support
		// compiled in for the requested filter.
		return filter->id <= LZMA_VLI_MAX
				? LZMA_OPTIONS_ERROR : LZMA_PROG_ERROR;
	}

	if (fe->props_size_get == NULL) {
		// No props_size_get() function, use props_size_fixed.
		*size = fe->props_size_fixed;
		return LZMA_OK;
	}

	return fe->props_size_get(size, filter->options);
}


extern LZMA_API(lzma_ret)
lzma_properties_encode(const lzma_filter *filter, uint8_t *props)
{
	const lzma_filter_encoder *const fe = encoder_find(filter->id);
	if (fe == NULL)
		return LZMA_PROG_ERROR;

	if (fe->props_encode == NULL)
		return LZMA_OK;

	return fe->props_encode(filter->options, props);
}
