/*
 *			GPAC - Multimedia Framework C SDK
 *
 *			Authors: Jean Le Feuvre
 *			Copyright (c) Telecom ParisTech 2000-2012
 *					All rights reserved
 *
 *  This file is part of GPAC / Media Tools sub-project
 *
 *  GPAC is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  GPAC is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */



#include <gpac/internal/media_dev.h>
#include <gpac/constants.h>
#include <gpac/config_file.h>

#ifndef GPAC_DISABLE_ISOM_WRITE
GF_EXPORT
GF_Err gf_media_change_par(GF_ISOFile *file, u32 track, s32 ar_num, s32 ar_den)
{
	u32 tk_w, tk_h, stype;
	GF_Err e;

	e = gf_isom_get_visual_info(file, track, 1, &tk_w, &tk_h);
	if (e) return e;

	stype = gf_isom_get_media_subtype(file, track, 1);
	if ((stype==GF_ISOM_SUBTYPE_AVC_H264) || (stype==GF_ISOM_SUBTYPE_AVC2_H264) ) {
#ifndef GPAC_DISABLE_AV_PARSERS
		GF_AVCConfig *avcc = gf_isom_avc_config_get(file, track, 1);
		AVC_ChangePAR(avcc, ar_num, ar_den);
		e = gf_isom_avc_config_update(file, track, 1, avcc);
		gf_odf_avc_cfg_del(avcc);
		if (e) return e;
#endif
	}
	else if (stype==GF_ISOM_SUBTYPE_MPEG4) {
		GF_ESD *esd = gf_isom_get_esd(file, track, 1);
		if (!esd || !esd->decoderConfig || (esd->decoderConfig->streamType!=4) ) {
			if (esd)  gf_odf_desc_del((GF_Descriptor *) esd);
			return GF_NOT_SUPPORTED;
		}
#ifndef GPAC_DISABLE_AV_PARSERS
		if (esd->decoderConfig->objectTypeIndication==GPAC_OTI_VIDEO_MPEG4_PART2) {
			e = gf_m4v_rewrite_par(&esd->decoderConfig->decoderSpecificInfo->data, &esd->decoderConfig->decoderSpecificInfo->dataLength, ar_num, ar_den);
			if (!e) e = gf_isom_change_mpeg4_description(file, track, 1, esd);
			gf_odf_desc_del((GF_Descriptor *) esd);
			if (e) return e;
		}
#endif
	} else {
		return GF_BAD_PARAM;
	}

	e = gf_isom_set_pixel_aspect_ratio(file, track, 1, ar_num, ar_den);
	if (e) return e;

	if ((ar_den>=0) && (ar_num>=0)) {
		if (ar_den) tk_w = tk_w * ar_num / ar_den;
		else if (ar_num) tk_h = tk_h * ar_den / ar_num;
	}
	/*revert to full frame for track layout*/
	else {
		e = gf_isom_get_visual_info(file, track, 1, &tk_w, &tk_h);
		if (e) return e;
	}
	return gf_isom_set_track_layout_info(file, track, tk_w<<16, tk_h<<16, 0, 0, 0);
}
#endif /*GPAC_DISABLE_ISOM_WRITE*/

GF_EXPORT
GF_Err gf_media_get_file_hash(const char *file, u8 hash[20]) 
{
#ifdef GPAC_DISABLE_CORE_TOOLS
	return GF_NOT_SUPPORTED;
#else
	u8 block[1024];
	u32 read;
	u64 size, tot;
	FILE *in;
	GF_SHA1Context *ctx;
#ifndef GPAC_DISABLE_ISOM
	GF_BitStream *bs = NULL;
	Bool is_isom = gf_isom_probe_file(file);
#endif

	in = gf_f64_open(file, "rb");
	gf_f64_seek(in, 0, SEEK_END);
	size = gf_f64_tell(in);
	gf_f64_seek(in, 0, SEEK_SET);

	ctx = gf_sha1_starts();
	tot = 0;
#ifndef GPAC_DISABLE_ISOM
	if (is_isom) bs = gf_bs_from_file(in, GF_BITSTREAM_READ);
#endif

	while (tot<size) {
#ifndef GPAC_DISABLE_ISOM
		if (is_isom) {
			u64 box_size = gf_bs_peek_bits(bs, 32, 0);
			u32 box_type = gf_bs_peek_bits(bs, 32, 4);

			/*till end of file*/
			if (!box_size) box_size = size-tot;
			/*64-bit size*/
			else if (box_size==1) box_size = gf_bs_peek_bits(bs, 64, 8);

			/*skip all MutableDRMInformation*/
			if (box_type==GF_4CC('m','d','r','i')) {
				gf_bs_skip_bytes(bs, box_size);
				tot += box_size;
			} else {
				u32 bsize = 0;
				while (bsize<box_size) {
					u32 to_read = (u32) ((box_size-bsize<1024) ? (box_size-bsize) : 1024);
					gf_bs_read_data(bs, block, to_read);
					gf_sha1_update(ctx, block, to_read);
					bsize += to_read;
				}
				tot += box_size;
			}
		} else
#endif
		{
			read = fread(block, 1, 1024, in);
			gf_sha1_update(ctx, block, read);
			tot += read;
		}
	}
	gf_sha1_finish(ctx, hash);
#ifndef GPAC_DISABLE_ISOM
	if (bs) gf_bs_del(bs);
#endif
	fclose(in);
	return GF_OK;
#endif
}

#ifndef GPAC_DISABLE_ISOM

#ifndef GPAC_DISABLE_ISOM_WRITE

static const u32 ISMA_VIDEO_OD_ID = 20;
static const u32 ISMA_AUDIO_OD_ID = 10;

static const u32 ISMA_VIDEO_ES_ID = 201;
static const u32 ISMA_AUDIO_ES_ID = 101;

static const char ISMA_BIFS_CONFIG[] = {0x00, 0x00, 0x60 };

/*ISMA audio*/
static const u8 ISMA_BIFS_AUDIO[] =
{
	0xC0, 0x10, 0x12, 0x81, 0x30, 0x2A, 0x05, 0x7C
};
/*ISMA video*/
static const u8 ISMA_GF_BIFS_VIDEO[] =
{
	0xC0, 0x10, 0x12, 0x60, 0x42, 0x82, 0x28, 0x29,
	0xD0, 0x4F, 0x00
};
/*ISMA audio-video*/
static const u8 ISMA_BIFS_AV[] =
{
	0xC0, 0x10, 0x12, 0x81, 0x30, 0x2A, 0x05, 0x72,
	0x60, 0x42, 0x82, 0x28, 0x29, 0xD0, 0x4F, 0x00
};

/*image only - uses same visual OD ID as video*/
static const u8 ISMA_BIFS_IMAGE[] =
{
	0xC0, 0x11, 0xA4, 0xCD, 0x53, 0x6A, 0x0A, 0x44,
	0x13, 0x00
};

/*ISMA audio-image*/
static const u8 ISMA_BIFS_AI[] =
{
	0xC0, 0x11, 0xA5, 0x02, 0x60, 0x54, 0x0A, 0xE4,
	0xCD, 0x53, 0x6A, 0x0A, 0x44, 0x13, 0x00
};


GF_EXPORT
GF_Err gf_media_make_isma(GF_ISOFile *mp4file, Bool keepESIDs, Bool keepImage, Bool no_ocr)
{
	u32 AudioTrack, VideoTrack, Tracks, i, mType, bifsT, odT, descIndex, VID, AID, bifsID, odID;
	u32 bifs, w, h;
	Bool is_image, image_track;
	GF_ESD *a_esd, *v_esd, *_esd;
	GF_ObjectDescriptor *od;
	GF_ODUpdate *odU;
	GF_ODCodec *codec;
	GF_ISOSample *samp;
	GF_BitStream *bs;
	u8 audioPL, visualPL;

	switch (gf_isom_get_mode(mp4file)) {
	case GF_ISOM_OPEN_EDIT:
	case GF_ISOM_OPEN_WRITE:
	case GF_ISOM_WRITE_EDIT:
		break;
	default:
		return GF_BAD_PARAM;
	}


	Tracks = gf_isom_get_track_count(mp4file);
	AID = VID = 0;
	is_image = 0;

	//search for tracks
	for (i=0; i<Tracks; i++) {
		GF_ESD *esd = gf_isom_get_esd(mp4file, i+1, 1);
		//remove from IOD
		gf_isom_remove_track_from_root_od(mp4file, i+1);

		mType = gf_isom_get_media_type(mp4file, i+1);
		switch (mType) {
		case GF_ISOM_MEDIA_VISUAL:
			image_track = 0;
			if (esd && ((esd->decoderConfig->objectTypeIndication==GPAC_OTI_IMAGE_JPEG) || (esd->decoderConfig->objectTypeIndication==GPAC_OTI_IMAGE_PNG)) )
				image_track = 1;

			/*remove image tracks if wanted*/
			if (keepImage || !image_track) {
				/*only ONE video stream possible with ISMA*/
				if (VID) {
					if (esd) gf_odf_desc_del((GF_Descriptor*)esd);
					GF_LOG(GF_LOG_ERROR, GF_LOG_AUTHOR, ("[ISMA convert] More than one video track found, cannot convert file - remove extra track(s)\n"));
					return GF_NOT_SUPPORTED;
				}
				VID = gf_isom_get_track_id(mp4file, i+1);
				is_image = image_track;
			} else {
				GF_LOG(GF_LOG_INFO, GF_LOG_AUTHOR, ("[ISMA convert] Visual track ID %d: only one sample found, assuming image and removing track\n", gf_isom_get_track_id(mp4file, i+1) ) );
				gf_isom_remove_track(mp4file, i+1);
				i -= 1;
				Tracks = gf_isom_get_track_count(mp4file);
			}
			break;
		case GF_ISOM_MEDIA_AUDIO:
			if (AID) {
				if (esd) gf_odf_desc_del((GF_Descriptor*)esd);
				GF_LOG(GF_LOG_ERROR, GF_LOG_AUTHOR, ("[ISMA convert] More than one audio track found, cannot convert file - remove extra track(s)\n") );
				return GF_NOT_SUPPORTED;
			}
			AID = gf_isom_get_track_id(mp4file, i+1);
			break;
		/*clean file*/
		default:
			if (mType==GF_ISOM_MEDIA_HINT) {
				GF_LOG(GF_LOG_INFO, GF_LOG_AUTHOR, ("[ISMA convert] Removing Hint track ID %d\n", gf_isom_get_track_id(mp4file, i+1) ));
			} else {
				GF_LOG(GF_LOG_INFO, GF_LOG_AUTHOR, ("[ISMA convert] Removing track ID %d\n", gf_isom_get_track_id(mp4file, i+1) ));
			}
			gf_isom_remove_track(mp4file, i+1);
			i -= 1;
			Tracks = gf_isom_get_track_count(mp4file);
			break;
		}
		if (esd) gf_odf_desc_del((GF_Descriptor*)esd);
	}
	//no audio no video
	if (!AID && !VID) return GF_OK;

	/*reset all PLs*/
	visualPL = 0xFE;
	audioPL = 0xFE;

	od = (GF_ObjectDescriptor *) gf_isom_get_root_od(mp4file);
	if (od && (od->tag==GF_ODF_IOD_TAG)) {
		audioPL = ((GF_InitialObjectDescriptor*)od)->audio_profileAndLevel;
		visualPL = ((GF_InitialObjectDescriptor*)od)->visual_profileAndLevel;
	}
	if (od) gf_odf_desc_del((GF_Descriptor *)od);


	//create the OD AU
	bifs = 0;
	odU = (GF_ODUpdate *) gf_odf_com_new(GF_ODF_OD_UPDATE_TAG);

	a_esd = v_esd = NULL;

	gf_isom_set_root_od_id(mp4file, 1);

	bifsID = 1;
	odID = 2;
	if (keepESIDs) {
		bifsID = 1;
		while ((bifsID==AID) || (bifsID==VID)) bifsID++;
		odID = 2;
		while ((odID==AID) || (odID==VID) || (odID==bifsID)) odID++;

	}

	VideoTrack = gf_isom_get_track_by_id(mp4file, VID);
	AudioTrack = gf_isom_get_track_by_id(mp4file, AID);

	w = h = 0;
	if (VideoTrack) {
		bifs = 1;
		od = (GF_ObjectDescriptor *) gf_odf_desc_new(GF_ODF_OD_TAG);
		od->objectDescriptorID = ISMA_VIDEO_OD_ID;

		if (!keepESIDs && (VID != ISMA_VIDEO_ES_ID)) {
			gf_isom_set_track_id(mp4file, VideoTrack, ISMA_VIDEO_ES_ID);
		}

		v_esd = gf_isom_get_esd(mp4file, VideoTrack, 1);
		if (v_esd) {
			v_esd->OCRESID = no_ocr ? 0 : bifsID;

			gf_odf_desc_add_desc((GF_Descriptor *)od, (GF_Descriptor *)v_esd);
			gf_list_add(odU->objectDescriptors, od);

			gf_isom_get_track_layout_info(mp4file, VideoTrack, &w, &h, NULL, NULL, NULL);
			if (!w || !h) {
				gf_isom_get_visual_info(mp4file, VideoTrack, 1, &w, &h);
#ifndef GPAC_DISABLE_AV_PARSERS
				if ((v_esd->decoderConfig->objectTypeIndication==GPAC_OTI_VIDEO_MPEG4_PART2) && (v_esd->decoderConfig->streamType==GF_STREAM_VISUAL)) {
					GF_M4VDecSpecInfo dsi;
					gf_m4v_get_config(v_esd->decoderConfig->decoderSpecificInfo->data, v_esd->decoderConfig->decoderSpecificInfo->dataLength, &dsi);
					if (!is_image && (!w || !h)) {
						w = dsi.width;
						h = dsi.height;
						gf_isom_set_visual_info(mp4file, VideoTrack, 1, w, h);
						GF_LOG(GF_LOG_INFO, GF_LOG_AUTHOR, ("[ISMA convert] Adjusting visual track size to %d x %d\n", w, h));
					}
					if (dsi.par_num && (dsi.par_den!=dsi.par_num)) {
						w *= dsi.par_num;
						w /= dsi.par_den;
					}
					if (dsi.VideoPL) visualPL = dsi.VideoPL;
				}
#endif
			}
		}
	}

	if (AudioTrack) {
		od = (GF_ObjectDescriptor *) gf_odf_desc_new(GF_ODF_OD_TAG);
		od->objectDescriptorID = ISMA_AUDIO_OD_ID;

		if (!keepESIDs && (AID != ISMA_AUDIO_ES_ID)) {
			gf_isom_set_track_id(mp4file, AudioTrack, ISMA_AUDIO_ES_ID);
		}

		a_esd = gf_isom_get_esd(mp4file, AudioTrack, 1);
		if (a_esd) {
			a_esd->OCRESID = no_ocr ? 0 : bifsID;

			if (!keepESIDs) a_esd->ESID = ISMA_AUDIO_ES_ID;
			gf_odf_desc_add_desc((GF_Descriptor *)od, (GF_Descriptor *)a_esd);
			gf_list_add(odU->objectDescriptors, od);
			if (!bifs) {
				bifs = 3;
			} else {
				bifs = 2;
			}

#ifndef GPAC_DISABLE_AV_PARSERS
			if (a_esd->decoderConfig->objectTypeIndication == GPAC_OTI_AUDIO_AAC_MPEG4) {
				GF_M4ADecSpecInfo cfg;
				gf_m4a_get_config(a_esd->decoderConfig->decoderSpecificInfo->data, a_esd->decoderConfig->decoderSpecificInfo->dataLength, &cfg);
				audioPL = cfg.audioPL;
			}
#endif
		}
	}

	/*update video cfg if needed*/
	if (v_esd) gf_isom_change_mpeg4_description(mp4file, VideoTrack, 1, v_esd);
	if (a_esd) gf_isom_change_mpeg4_description(mp4file, AudioTrack, 1, a_esd);

	/*likely 3GP or other files...*/
	if ((!a_esd && AudioTrack) || (!v_esd && VideoTrack)) return GF_OK;

	//get the OD sample
	codec = gf_odf_codec_new();
	samp = gf_isom_sample_new();
	gf_odf_codec_add_com(codec, (GF_ODCom *)odU);
	gf_odf_codec_encode(codec, 1);
	gf_odf_codec_get_au(codec, &samp->data, &samp->dataLength);
	gf_odf_codec_del(codec);
	samp->CTS_Offset = 0;
	samp->DTS = 0;
	samp->IsRAP = 1;

	/*create the OD track*/
	odT = gf_isom_new_track(mp4file, odID, GF_ISOM_MEDIA_OD, gf_isom_get_timescale(mp4file));
	if (!odT) return gf_isom_last_error(mp4file);

	_esd = gf_odf_desc_esd_new(SLPredef_MP4);
	_esd->decoderConfig->bufferSizeDB = samp->dataLength;
	_esd->decoderConfig->objectTypeIndication = GPAC_OTI_OD_V1;
	_esd->decoderConfig->streamType = GF_STREAM_OD;
	_esd->ESID = odID;
	_esd->OCRESID = no_ocr ? 0 : bifsID;
	gf_isom_new_mpeg4_description(mp4file, odT, _esd, NULL, NULL, &descIndex);
	gf_odf_desc_del((GF_Descriptor *)_esd);
	gf_isom_add_sample(mp4file, odT, 1, samp);
	gf_isom_sample_del(&samp);

	gf_isom_set_track_group(mp4file, odT, 1);

	/*create the BIFS track*/
	bifsT = gf_isom_new_track(mp4file, bifsID, GF_ISOM_MEDIA_SCENE, gf_isom_get_timescale(mp4file));
	if (!bifsT) return gf_isom_last_error(mp4file);

	_esd = gf_odf_desc_esd_new(SLPredef_MP4);
	_esd->decoderConfig->bufferSizeDB = sizeof(ISMA_BIFS_CONFIG);
	_esd->decoderConfig->objectTypeIndication = GPAC_OTI_SCENE_BIFS_V2;
	_esd->decoderConfig->streamType = GF_STREAM_SCENE;
	_esd->ESID = bifsID;
	_esd->OCRESID = 0;

	/*rewrite ISMA BIFS cfg*/
	bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
	/*empty bifs stuff*/
	gf_bs_write_int(bs, 0, 17);
	/*command stream*/
	gf_bs_write_int(bs, 1, 1);
	/*in pixel metrics*/
	gf_bs_write_int(bs, 1, 1);
	/*with size*/
	gf_bs_write_int(bs, 1, 1);
	gf_bs_write_int(bs, w, 16);
	gf_bs_write_int(bs, h, 16);
	gf_bs_align(bs);
	gf_bs_get_content(bs, &_esd->decoderConfig->decoderSpecificInfo->data, &_esd->decoderConfig->decoderSpecificInfo->dataLength);
	gf_isom_new_mpeg4_description(mp4file, bifsT, _esd, NULL, NULL, &descIndex);
	gf_odf_desc_del((GF_Descriptor *)_esd);
	gf_bs_del(bs);
	gf_isom_set_visual_info(mp4file, bifsT, descIndex, w, h);

	samp = gf_isom_sample_new();
	samp->CTS_Offset = 0;
	samp->DTS = 0;
	switch (bifs) {
	case 1:
		if (is_image) {
			samp->data = (char *) ISMA_BIFS_IMAGE;
			samp->dataLength = 10;
		} else {
			samp->data = (char *) ISMA_GF_BIFS_VIDEO;
			samp->dataLength = 11;
		}
		break;
	case 2:
		if (is_image) {
			samp->data = (char *) ISMA_BIFS_AI;
			samp->dataLength = 15;
		} else {
			samp->data = (char *) ISMA_BIFS_AV;
			samp->dataLength = 16;
		}
		break;
	case 3:
		samp->data = (char *) ISMA_BIFS_AUDIO;
		samp->dataLength = 8;
		break;
	}

	samp->IsRAP = 1;

	gf_isom_add_sample(mp4file, bifsT, 1, samp);
	samp->data = NULL;
	gf_isom_sample_del(&samp);
	gf_isom_set_track_group(mp4file, bifsT, 1);

	gf_isom_set_track_enabled(mp4file, bifsT, 1);
	gf_isom_set_track_enabled(mp4file, odT, 1);
	gf_isom_add_track_to_root_od(mp4file, bifsT);
	gf_isom_add_track_to_root_od(mp4file, odT);

	gf_isom_set_pl_indication(mp4file, GF_ISOM_PL_SCENE, 1);
	gf_isom_set_pl_indication(mp4file, GF_ISOM_PL_GRAPHICS, 1);
	gf_isom_set_pl_indication(mp4file, GF_ISOM_PL_OD, 1);
	gf_isom_set_pl_indication(mp4file, GF_ISOM_PL_AUDIO, audioPL);
	gf_isom_set_pl_indication(mp4file, GF_ISOM_PL_VISUAL, (u8) (is_image ? 0xFE : visualPL));

	gf_isom_set_brand_info(mp4file, GF_ISOM_BRAND_MP42, 1);
	gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_ISOM, 1);
	gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP5, 1);
	return GF_OK;
}


GF_EXPORT
GF_Err gf_media_make_3gpp(GF_ISOFile *mp4file)
{
	u32 Tracks, i, mType, stype, nb_vid, nb_avc, nb_aud, nb_txt, nb_non_mp4, nb_dims;
	Bool is_3g2 = 0;

	switch (gf_isom_get_mode(mp4file)) {
	case GF_ISOM_OPEN_EDIT:
	case GF_ISOM_OPEN_WRITE:
	case GF_ISOM_WRITE_EDIT:
		break;
	default:
		return GF_BAD_PARAM;
	}

	Tracks = gf_isom_get_track_count(mp4file);
	nb_vid = nb_aud = nb_txt = nb_avc = nb_non_mp4 = nb_dims = 0;

	for (i=0; i<Tracks; i++) {
		gf_isom_remove_track_from_root_od(mp4file, i+1);

		mType = gf_isom_get_media_type(mp4file, i+1);
		stype = gf_isom_get_media_subtype(mp4file, i+1, 1);
		switch (mType) {
		case GF_ISOM_MEDIA_VISUAL:
			/*remove image tracks if wanted*/
			if (gf_isom_get_sample_count(mp4file, i+1)<=1) {
				GF_LOG(GF_LOG_INFO, GF_LOG_AUTHOR, ("[3GPP convert] Visual track ID %d: only one sample found, assuming image and removing track\n", gf_isom_get_track_id(mp4file, i+1) ));
				goto remove_track;
			}

			if (stype == GF_ISOM_SUBTYPE_MPEG4_CRYP) gf_isom_get_ismacryp_info(mp4file, i+1, 1, &stype, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

			switch (stype) {
			case GF_ISOM_SUBTYPE_3GP_H263:
				nb_vid++;
				nb_non_mp4 ++;
				break;
			case GF_ISOM_SUBTYPE_AVC_H264:
			case GF_ISOM_SUBTYPE_AVC2_H264:
			case GF_ISOM_SUBTYPE_SVC_H264:
				nb_vid++;
				nb_avc++;
				break;
			case GF_ISOM_SUBTYPE_MPEG4:
				{
					GF_ESD *esd = gf_isom_get_esd(mp4file, i+1, 1);
					/*both MPEG4-Video and H264/AVC are supported*/
					if ((esd->decoderConfig->objectTypeIndication==GPAC_OTI_VIDEO_MPEG4_PART2) || (esd->decoderConfig->objectTypeIndication==GPAC_OTI_VIDEO_AVC) ) {
						nb_vid++;
					} else {
						GF_LOG(GF_LOG_INFO, GF_LOG_AUTHOR, ("[3GPP convert] Video format not supported by 3GP - removing track ID %d\n", gf_isom_get_track_id(mp4file, i+1) ));
						goto remove_track;
					}
					gf_odf_desc_del((GF_Descriptor *)esd);
				}
				break;
			default:
				GF_LOG(GF_LOG_INFO, GF_LOG_AUTHOR, ("[3GPP convert] Video format not supported by 3GP - removing track ID %d\n", gf_isom_get_track_id(mp4file, i+1) ));
				goto remove_track;
			}
			break;
		case GF_ISOM_MEDIA_AUDIO:
			if (stype == GF_ISOM_SUBTYPE_MPEG4_CRYP) gf_isom_get_ismacryp_info(mp4file, i+1, 1, &stype, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
			switch (stype) {
			case GF_ISOM_SUBTYPE_3GP_EVRC:
			case GF_ISOM_SUBTYPE_3GP_QCELP:
			case GF_ISOM_SUBTYPE_3GP_SMV:
				is_3g2 = 1;
				nb_aud++;
				break;
			case GF_ISOM_SUBTYPE_3GP_AMR:
			case GF_ISOM_SUBTYPE_3GP_AMR_WB:
				nb_aud++;
				nb_non_mp4 ++;
				break;
			case GF_ISOM_SUBTYPE_MPEG4:
			{
				GF_ESD *esd = gf_isom_get_esd(mp4file, i+1, 1);
				switch (esd->decoderConfig->objectTypeIndication) {
				case GPAC_OTI_AUDIO_13K_VOICE:
				case GPAC_OTI_AUDIO_EVRC_VOICE:
				case GPAC_OTI_AUDIO_SMV_VOICE:
					is_3g2 = 1;
				case GPAC_OTI_AUDIO_AAC_MPEG4:
					nb_aud++;
					break;
				default:
					GF_LOG(GF_LOG_INFO, GF_LOG_AUTHOR, ("[3GPP convert] Audio format not supported by 3GP - removing track ID %d\n", gf_isom_get_track_id(mp4file, i+1) ));
					goto remove_track;
				}
				gf_odf_desc_del((GF_Descriptor *)esd);
			}
				break;
			default:
				GF_LOG(GF_LOG_INFO, GF_LOG_AUTHOR, ("[3GPP convert] Audio format not supported by 3GP - removing track ID %d\n", gf_isom_get_track_id(mp4file, i+1) ));
				goto remove_track;
			}
			break;

		case GF_ISOM_MEDIA_SUBT:
			gf_isom_set_media_type(mp4file, i+1, GF_ISOM_MEDIA_TEXT);
		case GF_ISOM_MEDIA_TEXT:
			nb_txt++;
			break;

		case GF_ISOM_MEDIA_SCENE:
			if (stype == GF_ISOM_MEDIA_DIMS) {
				nb_dims++;
				break;
			}
		/*clean file*/
		default:
			if (mType==GF_ISOM_MEDIA_HINT) {
				GF_LOG(GF_LOG_INFO, GF_LOG_AUTHOR, ("[3GPP convert] Removing Hint track ID %d\n", gf_isom_get_track_id(mp4file, i+1) ));
			} else {
				GF_LOG(GF_LOG_INFO, GF_LOG_AUTHOR, ("[3GPP convert] Removing system track ID %d\n", gf_isom_get_track_id(mp4file, i+1) ));
			}

remove_track:
			gf_isom_remove_track(mp4file, i+1);
			i -= 1;
			Tracks = gf_isom_get_track_count(mp4file);
			break;
		}
	}

	/*no more IOD*/
	gf_isom_remove_root_od(mp4file);

	if (is_3g2) {
		gf_isom_set_brand_info(mp4file, GF_ISOM_BRAND_3G2A, 65536);
		gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP6, 0);
		gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP5, 0);
		gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GG6, 0);
		GF_LOG(GF_LOG_INFO, GF_LOG_AUTHOR, ("[3GPP convert] Setting major brand to 3GPP2\n"));
	} else {
		/*update FType*/
		if ((nb_vid>1) || (nb_aud>1) || (nb_txt>1)) {
			/*3GPP general purpose*/
			gf_isom_set_brand_info(mp4file, GF_ISOM_BRAND_3GG6, 1024);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP6, 0);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP5, 0);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP4, 0);
			GF_LOG(GF_LOG_INFO, GF_LOG_AUTHOR, ("[3GPP convert] Setting major brand to 3GPP Generic file\n"));
		}
		/*commented for QT compatibility, although this is correct (qt doesn't understand 3GP6 brand)*/
		else if (nb_txt && 0) {
			gf_isom_set_brand_info(mp4file, GF_ISOM_BRAND_3GP6, 1024);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP5, 1);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP4, 1);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GG6, 0);
			GF_LOG(GF_LOG_INFO, GF_LOG_AUTHOR, ("[3GPP convert] Setting major brand to 3GPP V6 file\n"));
		} else if (nb_avc) {
			gf_isom_set_brand_info(mp4file, GF_ISOM_BRAND_3GP6, 0/*1024*/);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_AVC1, 1);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP5, 0);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP4, 0);
			GF_LOG(GF_LOG_INFO, GF_LOG_AUTHOR, ("[3GPP convert] Setting major brand to 3GPP V6 file + AVC compatible\n"));
		} else {
			gf_isom_set_brand_info(mp4file, nb_avc ? GF_ISOM_BRAND_3GP6 : GF_ISOM_BRAND_3GP5, 0/*1024*/);
			gf_isom_modify_alternate_brand(mp4file, nb_avc ? GF_ISOM_BRAND_3GP5 : GF_ISOM_BRAND_3GP6, 0);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GP4, 1);
			gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_3GG6, 0);
			GF_LOG(GF_LOG_INFO, GF_LOG_AUTHOR, ("[3GPP convert] Setting major brand to 3GPP V5 file\n"));
		}
	}
	/*add/remove MP4 brands and add isom*/
	gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_MP41, (u8) ((nb_avc||is_3g2||nb_non_mp4) ? 0 : 1));
	gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_MP42, (u8) (nb_non_mp4 ? 0 : 1));
	gf_isom_modify_alternate_brand(mp4file, GF_ISOM_BRAND_ISOM, 1);
	return GF_OK;
}

GF_EXPORT
GF_Err gf_media_make_psp(GF_ISOFile *mp4)
{
	u32 i, count;
	u32 nb_a, nb_v;
	/*psp track UUID*/
	bin128 psp_track_uuid = {0x55, 0x53, 0x4D, 0x54, 0x21, 0xD2, 0x4F, 0xCE, 0xBB, 0x88, 0x69, 0x5C, 0xFA, 0xC9, 0xC7, 0x40};
	u8 psp_track_sig [] = {0x00, 0x00, 0x00, 0x1C, 0x4D, 0x54, 0x44, 0x54, 0x00, 0x01, 0x00, 0x12, 0x00, 0x00, 0x00, 0x0A, 0x55, 0xC4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
	/*psp mov UUID*/
	//bin128 psp_uuid = {0x50, 0x52, 0x4F, 0x46, 0x21, 0xD2, 0x4F, 0xCE, 0xBB, 0x88, 0x69, 0x5C, 0xFA, 0xC9, 0xC7, 0x40};

	nb_a = nb_v = 0;
	count = gf_isom_get_track_count(mp4);
	for (i=0; i<count; i++) {
		switch (gf_isom_get_media_type(mp4, i+1)) {
		case GF_ISOM_MEDIA_VISUAL:
			nb_v++;
			break;
		case GF_ISOM_MEDIA_AUDIO:
			nb_a++;
			break;
		}
	}
	if ((nb_v != 1) && (nb_a!=1)) {
		GF_LOG(GF_LOG_ERROR, GF_LOG_AUTHOR, ("[PSP convert] Movies need one audio track and one video track\n" ));
		return GF_BAD_PARAM;
	}
	for (i=0; i<count; i++) {
		switch (gf_isom_get_media_type(mp4, i+1)) {
		case GF_ISOM_MEDIA_VISUAL:
		case GF_ISOM_MEDIA_AUDIO:
			/*if no edit list, add one*/
			if (!gf_isom_get_edit_segment_count(mp4, i+1)) {
				GF_ISOSample *samp = gf_isom_get_sample_info(mp4, i+1, 1, NULL, NULL);
				if (!samp) {
					GF_LOG(GF_LOG_ERROR, GF_LOG_AUTHOR, ("[PSP convert] Track ID %d adding an edit list failed\n", gf_isom_get_track_id(mp4, i+1) ));
					return 1;
				}
				gf_isom_remove_edit_segments(mp4, i+1);
				gf_isom_append_edit_segment(mp4, i+1, gf_isom_get_track_duration(mp4, i+1), samp->CTS_Offset, GF_ISOM_EDIT_NORMAL);
				gf_isom_sample_del(&samp);
			}
			/*add PSP UUID*/
			gf_isom_remove_uuid(mp4, i+1, psp_track_uuid);
			gf_isom_add_uuid(mp4, i+1, psp_track_uuid, (char *) psp_track_sig, 28);
			break;
		default:
			GF_LOG(GF_LOG_INFO, GF_LOG_AUTHOR, ("[PSP convert] Removing track ID %d\n", gf_isom_get_track_id(mp4, i+1) ));
			gf_isom_remove_track(mp4, i+1);
			i -= 1;
			count -= 1;
			break;
		}
	}
	gf_isom_set_brand_info(mp4, GF_4CC('M','S','N','V'), 0);
	gf_isom_modify_alternate_brand(mp4, GF_4CC('M','S','N','V'), 1);
	return GF_OK;
}


#endif /*GPAC_DISABLE_ISOM_WRITE*/

GF_EXPORT
GF_ESD *gf_media_map_esd(GF_ISOFile *mp4, u32 track)
{
	u32 type;
	GF_GenericSampleDescription *udesc;
	GF_BitStream *bs;
	GF_ESD *esd;

	u32 subtype = gf_isom_get_media_subtype(mp4, track, 1);
	/*all types with an official MPEG-4 mapping*/
	switch (subtype) {
	case GF_ISOM_SUBTYPE_MPEG4:
	case GF_ISOM_SUBTYPE_MPEG4_CRYP:
	case GF_ISOM_SUBTYPE_AVC_H264:
	case GF_ISOM_SUBTYPE_AVC2_H264:
	case GF_ISOM_SUBTYPE_SVC_H264:
	case GF_ISOM_SUBTYPE_3GP_EVRC:
	case GF_ISOM_SUBTYPE_3GP_QCELP:
	case GF_ISOM_SUBTYPE_3GP_SMV:
		return gf_isom_get_esd(mp4, track, 1);
	}

	switch (gf_isom_get_media_type(mp4, track)) {
	case GF_ISOM_MEDIA_TEXT:
	case GF_ISOM_MEDIA_SUBT:
		return gf_isom_get_esd(mp4, track, 1);
	}

	if ((subtype == GF_ISOM_SUBTYPE_3GP_AMR) || (subtype == GF_ISOM_SUBTYPE_3GP_AMR_WB)) {
		GF_3GPConfig *gpc = gf_isom_3gp_config_get(mp4, track, 1);
		esd = gf_odf_desc_esd_new(0);
		esd->slConfig->timestampResolution = gf_isom_get_media_timescale(mp4, track);
		esd->ESID = gf_isom_get_track_id(mp4, track);
		esd->OCRESID = esd->ESID;
		esd->decoderConfig->streamType = GF_STREAM_AUDIO;
		/*use private DSI*/
		esd->decoderConfig->objectTypeIndication = GPAC_OTI_MEDIA_GENERIC;
		bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
		/*format ext*/
		gf_bs_write_u32(bs, subtype);
		gf_bs_write_u32(bs, (subtype == GF_ISOM_SUBTYPE_3GP_AMR) ? 8000 : 16000);
		gf_bs_write_u16(bs, 1);
		gf_bs_write_u16(bs, (subtype == GF_ISOM_SUBTYPE_3GP_AMR) ? 160 : 320);
		gf_bs_write_u8(bs, 16);
		gf_bs_write_u8(bs, gpc ? gpc->frames_per_sample : 0);
		if (gpc) gf_free(gpc);
		gf_bs_get_content(bs, &esd->decoderConfig->decoderSpecificInfo->data, &esd->decoderConfig->decoderSpecificInfo->dataLength);
		gf_bs_del(bs);
		return esd;
	}

	if (subtype == GF_ISOM_SUBTYPE_3GP_H263) {
		u32 w, h;
		esd = gf_odf_desc_esd_new(0);
		esd->slConfig->timestampResolution = gf_isom_get_media_timescale(mp4, track);
		esd->ESID = gf_isom_get_track_id(mp4, track);
		esd->OCRESID = esd->ESID;
		esd->decoderConfig->streamType = GF_STREAM_VISUAL;
		/*use private DSI*/
		esd->decoderConfig->objectTypeIndication = GPAC_OTI_MEDIA_GENERIC;
		bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
		/*format ext*/
		gf_bs_write_u32(bs, GF_4CC('h', '2', '6', '3'));
		gf_isom_get_visual_info(mp4, track, 1, &w, &h);
		gf_bs_write_u16(bs, w);
		gf_bs_write_u16(bs, h);
		gf_bs_get_content(bs, &esd->decoderConfig->decoderSpecificInfo->data, &esd->decoderConfig->decoderSpecificInfo->dataLength);
		gf_bs_del(bs);
		return esd;
	}

	if ((subtype == GF_ISOM_SUBTYPE_AC3) || (subtype == GF_ISOM_SUBTYPE_SAC3)) {
		GF_AC3Config *ac3 = gf_isom_ac3_config_get(mp4, track, 1);
		esd = gf_odf_desc_esd_new(0);
		esd->slConfig->timestampResolution = gf_isom_get_media_timescale(mp4, track);
		esd->ESID = gf_isom_get_track_id(mp4, track);
		esd->OCRESID = esd->ESID;
		esd->decoderConfig->streamType = GF_STREAM_AUDIO;
		esd->decoderConfig->objectTypeIndication = (ac3 && ac3->is_ec3) ? GPAC_OTI_AUDIO_AC3_ENHANCED : GPAC_OTI_AUDIO_AC3;
		gf_odf_desc_del((GF_Descriptor*)esd->decoderConfig->decoderSpecificInfo);
		esd->decoderConfig->decoderSpecificInfo = NULL;
		if (ac3) gf_free(ac3);
		return esd;
	}

	if (subtype == GF_ISOM_SUBTYPE_3GP_DIMS) {
		GF_DIMSDescription dims;
		esd = gf_odf_desc_esd_new(0);
		esd->slConfig->timestampResolution = gf_isom_get_media_timescale(mp4, track);
		esd->ESID = gf_isom_get_track_id(mp4, track);
		esd->OCRESID = esd->ESID;
		esd->decoderConfig->streamType = GF_STREAM_SCENE;
		/*use private DSI*/
		esd->decoderConfig->objectTypeIndication = GPAC_OTI_SCENE_DIMS;
		gf_isom_get_dims_description(mp4, track, 1, &dims);
		bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
		/*format ext*/
		gf_bs_write_u8(bs, dims.profile);
		gf_bs_write_u8(bs, dims.level);
		gf_bs_write_int(bs, dims.pathComponents, 4);
		gf_bs_write_int(bs, dims.fullRequestHost, 1);
		gf_bs_write_int(bs, dims.streamType, 1);
		gf_bs_write_int(bs, dims.containsRedundant, 2);
		gf_bs_write_data(bs, (char*)dims.textEncoding, strlen(dims.textEncoding)+1);
		gf_bs_write_data(bs, (char*)dims.contentEncoding, strlen(dims.contentEncoding)+1);
		gf_bs_get_content(bs, &esd->decoderConfig->decoderSpecificInfo->data, &esd->decoderConfig->decoderSpecificInfo->dataLength);
		gf_bs_del(bs);
		return esd;
	}

	type = gf_isom_get_media_type(mp4, track);
	if ((type != GF_ISOM_MEDIA_AUDIO) && (type != GF_ISOM_MEDIA_VISUAL)) return NULL;

	esd = gf_odf_desc_esd_new(0);
	esd->OCRESID = esd->ESID = gf_isom_get_track_id(mp4, track);
	esd->slConfig->useTimestampsFlag = 1;
	esd->slConfig->timestampResolution = gf_isom_get_media_timescale(mp4, track);
	esd->decoderConfig->objectTypeIndication = GPAC_OTI_MEDIA_GENERIC;
	/*format ext*/
	bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
	gf_bs_write_u32(bs, subtype);
	udesc = gf_isom_get_generic_sample_description(mp4, track, 1);
	if (type==GF_ISOM_MEDIA_AUDIO) {
		esd->decoderConfig->streamType = GF_STREAM_AUDIO;
		gf_bs_write_u32(bs, udesc->samplerate);
		gf_bs_write_u16(bs, udesc->nb_channels);
		gf_bs_write_u16(bs, 0);
		gf_bs_write_u8(bs, udesc->bits_per_sample);
		gf_bs_write_u8(bs, 0);
	} else {
		esd->decoderConfig->streamType = GF_STREAM_VISUAL;
		gf_bs_write_u16(bs, udesc->width);
		gf_bs_write_u16(bs, udesc->height);
	}
	if (udesc && udesc->extension_buf_size) {
		gf_bs_write_data(bs, udesc->extension_buf, udesc->extension_buf_size);
		gf_free(udesc->extension_buf);
	}
	if (udesc) gf_free(udesc);
	gf_bs_get_content(bs, &esd->decoderConfig->decoderSpecificInfo->data, &esd->decoderConfig->decoderSpecificInfo->dataLength);
	gf_bs_del(bs);
	return esd;
}

#endif /*GPAC_DISABLE_ISOM*/

#ifndef GPAC_DISABLE_MEDIA_IMPORT
static s32 gf_get_DQId(GF_ISOFile *file, u32 track)
{
	GF_AVCConfig *svccfg;
	GF_ISOSample *samp;
	u32 di = 0;
	char *buffer;
	GF_BitStream *bs;
	u32 max_size = 4096;
	u32 size, nalu_size_length;
	u8 nal_type;

	svccfg = gf_isom_svc_config_get(file, track, 1);
	if (!svccfg)
		return 0;
	samp = gf_isom_get_sample(file, track, 1, &di);
	if (!samp)
		return -1;
	bs = gf_bs_new(samp->data, samp->dataLength, GF_BITSTREAM_READ);
	buffer = (char*)gf_malloc(sizeof(char) * max_size);
	nalu_size_length = 8 * svccfg->nal_unit_size;
	while (gf_bs_available(bs))
	{
		size = gf_bs_read_int(bs, nalu_size_length);
		if (size>max_size) {
			buffer = (char*)gf_realloc(buffer, sizeof(char)*size);
			max_size = size;
		}
		gf_bs_read_data(bs, buffer, size);
		nal_type = buffer[0] & 0x1F;
		if (nal_type == GF_AVC_NALU_SVC_SLICE)
			return buffer[2] & 0x7F;
	}
	return -1;
}
static u32 gf_isom_get_track_id_max(GF_ISOFile *file)
{
	u32 num_track, i, trackID;
	u32 max_id = 0;

	num_track = gf_isom_get_track_count(file);
	for (i = 1; i <= num_track; i++)
	{
		trackID = gf_isom_get_track_id(file, i);
		if (max_id < trackID)
			max_id = trackID;
	}

	return max_id;
}

/* Split SVC layers */
GF_EXPORT
GF_Err gf_media_split_svc(GF_ISOFile *file, u32 track, Bool splitAll)
{
	GF_AVCConfig *avccfg, *svccfg;
	u32 num_svc_track, num_sample, svc_track, dst_track, ref_trackID, ref_trackNum, max_id;
	u32 di, width, height, size, nalu_size_length;
	u32 i, j, t;
	GF_Err e; 
	GF_AVCConfigSlot *slc;
	AVCState avc;
	s32 sps_id, pps_id;
	GF_ISOSample *samp;
	GF_BitStream *bs;
	GF_BitStream ** sample_bs;
	u8 nal_type, nal_hdr;
	GF_ISOSample  *dst_samp;
	GF_BitStream *dst_bs;
	char *buffer; 
	//u8 dependency_id, quality_id, temporal_id, avc_dependency_id, avc_quality_id;
	u32 max_size = 4096;
	s32 *sps_track, *sps, *pps;
	u32 num_pps, num_sps;
	u64 offset;
	Bool is_splited, first_pps;
	Bool *first_sample_track;
	u64 *first_DTS_track;
	u32 NALUnitHeader;
	u8 track_ref_index;
	s8 sample_offset;
	u32 data_offset;
	u32 data_length;
	u32 count, timescale;

	avccfg = gf_isom_avc_config_get(file, track, 1);
	svccfg = gf_isom_svc_config_get(file, track, 1);

	is_splited = (avccfg) ? 0 : 1;

	/*if we have not any SVC -> stop*/
	if (!svccfg)
		return GF_OK;

	timescale = gf_isom_get_media_timescale(file, track);

	num_sps = gf_list_count(svccfg->sequenceParameterSets);
	num_pps = gf_list_count(svccfg->pictureParameterSets);
	/*we have a splited file with 1 SVC / track*/
	if (is_splited && num_pps == 1)
	{
		/*use 'all' mode -> stop*/
		if (splitAll)
			return GF_OK;
		/*use 'base' mode -> merge SVC tracks*/
		else
			return gf_media_merge_svc(file, track, 0);
	}
	num_svc_track = splitAll ? num_sps : 1;
	max_id = gf_isom_get_track_id_max(file);
	di = 0;
	
	memset(&avc, 0, sizeof(AVCState));
	avc.sps_active_idx = -1;
	nalu_size_length = 8 * svccfg->nal_unit_size;
	/*read all sps*/
	sps =  (s32 *) gf_malloc(num_sps * sizeof(s32));
	sps_track = (s32 *) gf_malloc(num_sps * sizeof(s32));
	for (i = 0; i < num_sps; i++)
	{
		slc = gf_list_get(svccfg->sequenceParameterSets, i);
		sps_id = AVC_ReadSeqInfo(slc->data+1, slc->size-1, &avc,0, NULL);
		if (sps_id < 0) {
			return GF_NON_COMPLIANT_BITSTREAM;
		}
		sps[i] = sps_id;
		sps_track[i] = i;
	}
	/*read all pps*/
	pps =  (s32 *) gf_malloc(num_pps * sizeof(s32));
	for (j = 0; j < num_pps; j++)
	{
		slc = gf_list_get(svccfg->pictureParameterSets, j);
		pps_id = AVC_ReadPictParamSet(slc->data+1, slc->size-1, &avc);
		if (pps_id < 0) {
			return GF_NON_COMPLIANT_BITSTREAM;
		}
		pps[j] = pps_id;
	}
	if (!is_splited)
		ref_trackID = gf_isom_get_track_id(file, track);
	else
	{
		gf_isom_get_reference(file, track, GF_ISOM_REF_BASE, 1, &ref_trackNum);
		ref_trackID = gf_isom_get_track_id(file, ref_trackNum);
	}
	/*read first sample for determinating the order of SVC tracks*/
	count = 0;
	samp = gf_isom_get_sample(file, track, 1, &di);
	if (!samp)
		return GF_IO_ERR;
	bs = gf_bs_new(samp->data, samp->dataLength, GF_BITSTREAM_READ);
	offset = 0;
	buffer = (char*)gf_malloc(sizeof(char) * max_size);
	while (gf_bs_available(bs))
	{
		size = gf_bs_read_int(bs, nalu_size_length);
		if (size>max_size) {
			buffer = (char*)gf_realloc(buffer, sizeof(char)*size);
			max_size = size;
		}
		nal_hdr = gf_bs_read_u8(bs);
		nal_type = nal_hdr & 0x1F;
		AVC_ParseNALU(bs, nal_hdr, &avc);
		gf_bs_seek(bs, offset+nalu_size_length/8);
		gf_bs_read_data(bs, buffer, size);
		offset += size + nalu_size_length/8;
		if (nal_type == GF_AVC_NALU_SVC_SLICE)
		{
			/*verify the order of SPS, reorder if necessary*/
			if (avc.s_info.pps->sps_id != sps[count])
			{
				for (i = count+1; i < num_sps; i++)
				{
					/*swap two SPS*/
					if (avc.s_info.pps->sps_id == sps[i])
					{
						sps[i] = sps[count];
						sps[count] = avc.s_info.pps->sps_id;
						sps_track[count] = i;
						break;
					}
				}
				/*for testing: ensure that there are not two NALU which use the same SPS, so that we can use sps for separating layers*/
				assert(i < num_sps);
			}
			count++;
		}
	}
	/*for testing: ensure that the number of SPS is equal to the number of SVC layers*/
	assert(count == num_sps);

	gf_free(buffer);
	buffer = NULL;
	
	for (t = 0; t < num_svc_track; t++)
	{
		GF_AVCConfig *cfg;

		e = GF_OK;
		svc_track = gf_isom_new_track(file, t+1+max_id, GF_ISOM_MEDIA_VISUAL, timescale);
		if (!svc_track) 
		{
			e = gf_isom_last_error(file);
			return e;
		}
		gf_isom_set_track_enabled(file, svc_track, 1);
		gf_isom_set_track_reference(file, svc_track, GF_ISOM_REF_BASE, ref_trackID);
		cfg = gf_odf_avc_cfg_new();
		cfg->complete_representation = 1; //SVC
		e = gf_isom_svc_config_new(file, svc_track, cfg, NULL, NULL, &di);
		if (e)
			return e;
		if (splitAll)
		{
			sps_id = sps[t];
			width = avc.sps[sps_id].width;
			height = avc.sps[sps_id].height;
			gf_isom_set_visual_info(file, svc_track, di, width, height);
			cfg->configurationVersion = 1;
			cfg->chroma_bit_depth = 8 + avc.sps[sps_id].chroma_bit_depth_m8;
			cfg->chroma_format = avc.sps[sps_id].chroma_format;
			cfg->luma_bit_depth = 8 + avc.sps[sps_id].luma_bit_depth_m8;
			cfg->profile_compatibility = avc.sps[sps_id].prof_compat;
			cfg->AVCLevelIndication = avc.sps[sps_id].level_idc;
			cfg->AVCProfileIndication = avc.sps[sps_id].profile_idc;
			cfg->nal_unit_size = svccfg->nal_unit_size;
			gf_list_add(cfg->sequenceParameterSets,  gf_list_get(svccfg->sequenceParameterSets, sps_track[t]));
			first_pps = 1;
			for (j = 0; j < num_pps; j++)
			{
				pps_id = pps[j];
				if (avc.pps[pps_id].sps_id == sps_id)
				{
					gf_list_add(cfg->pictureParameterSets,  gf_list_get(svccfg->pictureParameterSets, j));
				}
			}
		}
		else
		{
			for (i = 0; i < num_sps; i++)
			{
				sps_id = sps[i];
				width = avc.sps[sps_id].width;
				height = avc.sps[sps_id].height;
				gf_isom_set_visual_info(file, svc_track, di, width, height);
				cfg->configurationVersion = 1;
				cfg->chroma_bit_depth = 8 + avc.sps[sps_id].chroma_bit_depth_m8;
				cfg->chroma_format = avc.sps[sps_id].chroma_format;
				cfg->luma_bit_depth = 8 + avc.sps[sps_id].luma_bit_depth_m8;
				cfg->profile_compatibility = avc.sps[sps_id].prof_compat;
				cfg->AVCLevelIndication = avc.sps[sps_id].level_idc;
				cfg->AVCProfileIndication = avc.sps[sps_id].profile_idc;
				cfg->nal_unit_size = svccfg->nal_unit_size;
				gf_list_add(cfg->sequenceParameterSets,  gf_list_get(svccfg->sequenceParameterSets, sps_track[i]));
				for (j = 0; j < num_pps; j++)
				{
					slc = gf_list_get(svccfg->pictureParameterSets, t);
					pps_id = AVC_ReadPictParamSet(slc->data+1, slc->size-1, &avc);
					if (pps_id < 0) {
						return GF_NON_COMPLIANT_BITSTREAM;
					}
					if (avc.pps[pps_id].sps_id == sps_id)
					{
						gf_list_add(cfg->pictureParameterSets,  gf_list_get(svccfg->pictureParameterSets, j));
					}
				}
			}
		}
		gf_isom_svc_config_update(file, svc_track, 1, cfg, 0);
	}

	num_sample = gf_isom_get_sample_count(file, track);
	first_sample_track = (Bool *) gf_malloc((num_svc_track+1) * sizeof(Bool));
	for (t = 0; t <= num_svc_track; t++)
		first_sample_track[t] = 1;
	first_DTS_track = (u64 *) gf_malloc((num_svc_track+1) * sizeof(u64));
	for (t = 0; t <= num_svc_track; t++)
		first_DTS_track[t] = 0;

	for (i = 1; i <= num_sample; i++)
	{
		u32 *prev_layer;
		u32 count_prev_layer;

		prev_layer = (u32 *) gf_malloc(num_svc_track * sizeof(u32));
		count_prev_layer = 0;
		

		samp = gf_isom_get_sample(file, track, i, &di);
		if (!samp)
			return GF_IO_ERR;

		/* Create (num_svc_track) SVC bitstreams + 1 AVC bitstream*/
		sample_bs = (GF_BitStream **) gf_malloc(sizeof(GF_BitStream *) * (num_svc_track+1));
		for (j = 0; j <= num_svc_track; j++)
			sample_bs[j] = (GF_BitStream *) gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);

		bs = gf_bs_new(samp->data, samp->dataLength, GF_BITSTREAM_READ);
		offset = 0;
		buffer = (char*)gf_malloc(sizeof(char) * max_size);
		while (gf_bs_available(bs))
		{
			size = gf_bs_read_int(bs, nalu_size_length);
			if (size>max_size) {
				buffer = (char*)gf_realloc(buffer, sizeof(char)*size);
				max_size = size;
			}
			nal_hdr = gf_bs_read_u8(bs);
			nal_type = nal_hdr & 0x1F;
			AVC_ParseNALU(bs, nal_hdr, &avc);
			gf_bs_seek(bs, offset+nalu_size_length/8);
			gf_bs_read_data(bs, buffer, size);
			offset += size + nalu_size_length/8;
			
			switch (nal_type) {
				//case GF_AVC_NALU_SVC_PREFIX_NALU:
				case GF_AVC_NALU_SVC_SLICE:
					dst_track = 0;
					if (splitAll)
					{
						for (t = 0; t < num_svc_track; t++) // num_svc_track == num_pps
						{
							if (sps_track[t] == (avc.s_info.pps)->sps_id)
							{
								dst_track = t + 1;
								break;
							}
						}
					}
					else
						dst_track = 1;
					dst_bs = sample_bs[dst_track];
					/*write extractor*/
					if (!gf_bs_get_position(dst_bs))
					{
						//reference to base layer
						gf_bs_write_int(dst_bs, 14, nalu_size_length); // extractor 's size = 14
						NALUnitHeader = 0; //reset
						NALUnitHeader |= 0x1F000000; // NALU type = 31
						gf_bs_write_u32(dst_bs, NALUnitHeader);
						//track_ref_index is a trackID, not a trackNum. So when rewrite, gf_isom_get_track_id must be used to find trackNum
						track_ref_index = ref_trackID; 
						gf_bs_write_u8(dst_bs, track_ref_index);
						sample_offset = 0;
						gf_bs_write_u8(dst_bs, sample_offset);
						data_offset = 0;
						gf_bs_write_u32(dst_bs, data_offset);
						data_length = 0;
						gf_bs_write_u32(dst_bs, data_length);
						//reference to previous layer(s)
						for (t = 0; t < count_prev_layer; t++)
						{
							gf_bs_write_int(dst_bs, 14, nalu_size_length);
							NALUnitHeader = 0;
							NALUnitHeader |= 0x1F000000;
							gf_bs_write_u32(dst_bs, NALUnitHeader);
							track_ref_index = max_id + prev_layer[t];
							gf_bs_write_u8(dst_bs, track_ref_index);
							sample_offset = 0;
							gf_bs_write_u8(dst_bs, sample_offset);
							data_offset = (t+1) * (nalu_size_length/8 + 14); // (nalu_size_length/8) bytes of NALU length field + 14 bytes of extractor per layer
							gf_bs_write_u32(dst_bs, data_offset);
							data_length = 0;
							gf_bs_write_u32(dst_bs, data_length);
						}
					}
					prev_layer[count_prev_layer] = dst_track;
					count_prev_layer++;
					break;
				default:
					dst_bs = sample_bs[0];
			}

			gf_bs_write_int(dst_bs, size, nalu_size_length);
			gf_bs_write_data(dst_bs, buffer, size);
		}

		for (j = 0; j <= num_svc_track; j++)
		{
			if (gf_bs_get_position(sample_bs[j]))
			{
				if (first_sample_track[j])
				{
					first_sample_track[j] = 0;
					first_DTS_track[j] = samp->DTS;
				}
				dst_samp = gf_isom_sample_new();
				dst_samp->CTS_Offset = samp->CTS_Offset;
				dst_samp->DTS = samp->DTS - first_DTS_track[j];
				dst_samp->IsRAP = samp->IsRAP;
				gf_bs_get_content(sample_bs[j], &dst_samp->data, &dst_samp->dataLength);
				if (j) //SVC
					e = gf_isom_add_sample(file, track+j, di, dst_samp);
				else
					e = gf_isom_update_sample(file, track, i, dst_samp, 1);
				if (e)
					return e;
				gf_isom_sample_del(&dst_samp);
				dst_samp = NULL;
			}
			gf_bs_del(sample_bs[j]);
			sample_bs[j] = NULL;
		}
		gf_free(sample_bs);
		gf_free(bs);
	}

	/*add Editlist entry if DTS of the first sample is not zero*/
	for (t = 0; t <= num_svc_track; t++)
	{
		if (first_DTS_track[t])
		{
			u32 media_ts, moov_ts, offset;
			u64 dur;
			media_ts = gf_isom_get_media_timescale(file, t);
			moov_ts = gf_isom_get_timescale(file);
			offset = (u32)(first_DTS_track[t]) * moov_ts / media_ts;
			dur = gf_isom_get_media_duration(file, t) * moov_ts / media_ts;
			gf_isom_set_edit_segment(file, t, 0, offset, 0, GF_ISOM_EDIT_EMPTY);
			gf_isom_set_edit_segment(file, t, offset, dur, 0, GF_ISOM_EDIT_NORMAL);
		}
	}
	
	/*if this is a merged file: delete SVC config*/
	if (!is_splited)
		gf_isom_svc_config_del(file, track, 1);
	/*if this is as splited file: delete this track*/
	else
	{
		gf_isom_remove_track(file, track);
	}
	return GF_OK;
}

/* Merge SVC layers*/
GF_EXPORT
GF_Err gf_media_merge_svc(GF_ISOFile *file, u32 track, Bool mergeAll)
{
	GF_AVCConfig *avccfg, *svccfg, *cfg;
	u32 merge_track;
	u32 num_track, num_sample;
	GF_ISOSample *avc_samp, *samp, *dst_samp;
	GF_BitStream *bs, *dst_bs;
	GF_Err e;
	u32 size;
	u32 i, t;
	u32 di = 0;
	char *buffer;
	u32 max_size = 4096;
	u32 nalu_size_length;
	u32 ref_trackNum, ref_trackID;
	s32 *DQId;
	u32 count;
	u32 *list_track_sorted;
	u32 *cur_sample, *max_sample;
	u32 width = 0, height = 0;
	u64 *DTS_offset;
	u32 nb_EditList;
	u32 media_ts, moov_ts;
	u64 EditTime, SegmentDuration, MediaTime;
	u8 EditMode;
	Bool first_sample;
	u64 first_DTS, offset, dur;
	u32 max_id;
	u32 timescale;
	u8 nal_type;

	avc_samp = samp = NULL;

	avccfg = gf_isom_avc_config_get(file, track, 1);
	if (!avccfg && mergeAll)
		return GF_BAD_PARAM;
	timescale = gf_isom_get_media_timescale(file, track);

	num_track = gf_isom_get_track_count(file);
	if (num_track == 1) {
		if (avccfg) gf_odf_avc_cfg_del(avccfg);
		return GF_OK;
	}

	/*create a new merged track*/
	max_id = gf_isom_get_track_id_max(file);
	merge_track = gf_isom_new_track(file, max_id+1, GF_ISOM_MEDIA_VISUAL, timescale);
	gf_isom_set_track_enabled(file, merge_track, 1);
	/*add avc configuration if any*/
	if (avccfg)
		gf_isom_avc_config_new(file, merge_track, avccfg, NULL, NULL, &di);

	svccfg = gf_odf_avc_cfg_new();
	svccfg->complete_representation = 1; 
	e = gf_isom_svc_config_new(file, merge_track, svccfg, NULL, NULL, &di);
	if (e) goto exit;

	if (avccfg) {
		ref_trackNum = track;
		ref_trackID = gf_isom_get_track_id(file, track);
	} else {
		gf_isom_get_reference(file, track, GF_ISOM_REF_BASE, 1, &ref_trackNum);
		ref_trackID = gf_isom_get_track_id(file, ref_trackNum);
	}

	list_track_sorted = (u32 *) gf_malloc(num_track * sizeof(u32));
	DQId = (s32 *) gf_malloc(num_track * sizeof(s32));
	count = 0;
	for (t = 1; t <= num_track; t++) {
		u32 pos = 0;
		s32 track_DQId = gf_get_DQId(file, t);
		if (track_DQId < 0) {
			e = GF_ISOM_INVALID_MEDIA;
			goto exit;
		}
		if ((t != track) && !gf_isom_has_track_reference(file, t, GF_ISOM_REF_BASE, ref_trackID))
			continue;
		while ((pos < count ) && (DQId[pos] <= track_DQId))
			pos++;
		for (i = count; i > pos; i--)
		{
			list_track_sorted[i] = list_track_sorted[i-1];
			DQId[i] = DQId[i-1];
		}
		list_track_sorted[pos] = t;
		DQId[pos] = track_DQId;
		count++;
	}

	if (mergeAll)
	{
		gf_isom_get_visual_info(file, list_track_sorted[0], 1, &width, &height);
	}
	else
	{
		for (t = 0; t < count; t++)
			gf_isom_get_visual_info(file, list_track_sorted[t], 1, &width, &height);
	}
	gf_isom_set_visual_info(file, merge_track, 1, width, height);

	for (t = 0; t < count; t++)
	{
		cfg = gf_isom_svc_config_get(file, list_track_sorted[t], 1);
		if (!cfg)
			continue;
		svccfg->configurationVersion = 1;
		svccfg->chroma_bit_depth = cfg->chroma_bit_depth;
		svccfg->chroma_format = cfg->chroma_format;
		svccfg->luma_bit_depth = cfg->luma_bit_depth;
		svccfg->profile_compatibility = cfg->profile_compatibility;
		svccfg->AVCLevelIndication = cfg->AVCLevelIndication;
		svccfg->AVCProfileIndication = cfg->AVCProfileIndication;
		svccfg->nal_unit_size = cfg->nal_unit_size;
		for (i = 0; i < gf_list_count(cfg->sequenceParameterSets); i++)
		{
			gf_list_add(svccfg->sequenceParameterSets,  gf_list_get(cfg->sequenceParameterSets, i));
			}
		for (i = 0; i < gf_list_count(cfg->pictureParameterSets); i++)
		{
			gf_list_add(svccfg->pictureParameterSets, gf_list_get(cfg->pictureParameterSets, i));
		}
		if (mergeAll)
			gf_isom_svc_config_update(file, merge_track, 1, svccfg, 1);
		else
			gf_isom_svc_config_update(file, merge_track, 1, svccfg, 0);
		
		gf_odf_avc_cfg_del(cfg);
	}

	cur_sample = (u32 *) gf_malloc(count * sizeof(u32));
	max_sample = (u32 *) gf_malloc(count * sizeof(u32));
	for (t = 0; t < count; t++)
	{
		cur_sample[t] = 1;
		max_sample[t] = gf_isom_get_sample_count(file, list_track_sorted[t]);
	}

	DTS_offset = (u64 *) gf_malloc(count * sizeof(u64));
	for (t = 0; t < count; t++)
	{
		nb_EditList = gf_isom_get_edit_segment_count(file, list_track_sorted[t]);
		if (!nb_EditList)
			DTS_offset[t] = 0;
		else
		{
			media_ts = gf_isom_get_media_timescale(file, list_track_sorted[t]);
			moov_ts = gf_isom_get_timescale(file);
			for (i = 1; i <= nb_EditList; i++)
			{
				e = gf_isom_get_edit_segment(file, list_track_sorted[t], i, &EditTime, &SegmentDuration, &MediaTime, &EditMode);
				if (e) goto exit;

				if (!EditMode)
				{
					DTS_offset[t] = SegmentDuration * media_ts / moov_ts;
				}
			}
		}
	}

	num_sample = gf_isom_get_sample_count(file, ref_trackNum);
	nalu_size_length = 8 * svccfg->nal_unit_size;
	first_sample = 1;
	first_DTS = 0;
	for (i = 1; i <= num_sample; i++)
	{
		dst_bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
		/*add extractor if nessassary*/
		if (!mergeAll)
		{
			u32 NALUnitHeader = 0;
			u8 track_ref_index;
			s8 sample_offset;
			u32 data_offset;
			u32 data_length;

			gf_bs_write_int(dst_bs, 14, nalu_size_length); // extractor 's size = 14
			NALUnitHeader |= 0x1F000000; // NALU type = 31
			gf_bs_write_u32(dst_bs, NALUnitHeader);
			track_ref_index = ref_trackID;
			gf_bs_write_u8(dst_bs, track_ref_index);
			sample_offset = 0;
			gf_bs_write_u8(dst_bs, sample_offset);
			data_offset = 0;
			gf_bs_write_u32(dst_bs, data_offset);
			data_length = 0;
			gf_bs_write_u32(dst_bs, data_length);
		}
		buffer = (char*)gf_malloc(sizeof(char) * max_size);

		avc_samp = gf_isom_get_sample(file, ref_trackNum, i, &di);
		if (!avc_samp) {
			e = gf_isom_last_error(file);
			goto exit;
		}

		for (t = 0; t < count; t++)
		{
			if (cur_sample[t] > max_sample[t])
				continue;
			samp = gf_isom_get_sample(file, list_track_sorted[t], cur_sample[t], &di);
			if (!samp) {
				e = gf_isom_last_error(file);
				goto exit;
			}

			if ((samp->DTS + DTS_offset[t]) != avc_samp->DTS)
				continue;
			bs = gf_bs_new(samp->data, samp->dataLength, GF_BITSTREAM_READ);
			while (gf_bs_available(bs))
			{
				size = gf_bs_read_int(bs, nalu_size_length);
				if (size>max_size) {
					buffer = (char*)gf_realloc(buffer, sizeof(char)*size);
					max_size = size;
				}			
				gf_bs_read_data(bs, buffer, size);
				nal_type = buffer[0] & 0x1F;
				/*skip extractor*/
				if (nal_type == 31)
					continue;			
				/*copy to new bitstream*/
				gf_bs_write_int(dst_bs, size, nalu_size_length);
				gf_bs_write_data(dst_bs, buffer, size);
			}
			gf_bs_del(bs);
			bs = NULL;
			gf_isom_sample_del(&samp);
			samp = NULL;
			cur_sample[t]++;
		}
		
		/*add sapmle to track*/
		if (gf_bs_get_position(dst_bs))
		{
			if (first_sample)
			{
				first_DTS = avc_samp->DTS;
				first_sample = 0;
			}
			dst_samp = gf_isom_sample_new();
			dst_samp->CTS_Offset = avc_samp->CTS_Offset;
			dst_samp->DTS = avc_samp->DTS - first_DTS;
			dst_samp->IsRAP = avc_samp->IsRAP;
			gf_bs_get_content(dst_bs, &dst_samp->data, &dst_samp->dataLength);
			e = gf_isom_add_sample(file, merge_track, 1, dst_samp);
			gf_bs_del(dst_bs);
			dst_bs = NULL;
			gf_isom_sample_del(&dst_samp);
			if (e)
				goto exit;
		}
		gf_isom_sample_del(&avc_samp);
		avc_samp = NULL;
	}

	/*Add EditList if nessessary*/
	if (!first_DTS)
	{
		media_ts = gf_isom_get_media_timescale(file, merge_track);
		moov_ts = gf_isom_get_timescale(file);
		offset = (u32)(first_DTS) * moov_ts / media_ts;
		dur = gf_isom_get_media_duration(file, merge_track) * moov_ts / media_ts;
		gf_isom_set_edit_segment(file, merge_track, 0, offset, 0, GF_ISOM_EDIT_EMPTY);
		gf_isom_set_edit_segment(file, merge_track, offset, dur, 0, GF_ISOM_EDIT_NORMAL);
	}

	/*Delete SVC track(s) that references to ref_track*/
	for (t = 1; t <= num_track; t++)
	{
		if ((t != track) && !gf_isom_has_track_reference(file, t, GF_ISOM_REF_BASE, ref_trackID))
			continue;
		gf_isom_remove_track(file, t);
		num_track--; //we removed one track from file
		t--;
	}

exit:
	if (avc_samp) gf_isom_sample_del(&avc_samp);
	if (samp) gf_isom_sample_del(&samp);
	if (avccfg) gf_odf_avc_cfg_del(avccfg);
	if (svccfg) gf_odf_avc_cfg_del(svccfg);
	return e;
}

#endif /*GPAC_DISABLE_MEDIA_IMPORT*/

