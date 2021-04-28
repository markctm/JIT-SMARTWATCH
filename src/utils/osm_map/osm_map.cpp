/****************************************************************************
 *   Aug 3 12:17:11 2020
 *   Copyright  2020  Dirk Brosswick
 *   Email: dirk.brosswick@googlemail.com
 ****************************************************************************/
 
/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include "config.h"
#include <TTGO.h>
#include <HTTPClient.h>
#include <Update.h>
#include <SPIFFS.h>

#include "osm_map.h"
#include "utils/alloc.h"
#include "utils/uri_load/uri_load.h"

/**
 * @brief osm_map default tile image
 */
LV_IMG_DECLARE(osm_no_data_240px);
/**
 * @brief osm tile server list
 */
uint32_t osm_map_long2tilex(double lon, uint32_t z);
uint32_t osm_map_lat2tiley(double lat, uint32_t z);
double osm_map_tilex2long(int x, uint32_t z);
double osm_map_tiley2lat(int y, uint32_t z);
osm_location_t *osm_map_update_tile_image( osm_location_t *osm_location );
void osm_map_gen_url( osm_location_t *osm_location );

osm_location_t *osm_map_create_location_obj( void ) {
    /**
     * allocate osm_location structure
     */
    osm_location_t *osm_location = (osm_location_t*)MALLOC( sizeof( osm_location_t ) );
    /**
     * if allocation was successfull, set to default
     */
    if ( osm_location ) {
        osm_location->zoom = 16;                      
        osm_location->lon = 0;                         
        osm_location->lat = 0;                         
        osm_location->tilex = 0;                     
        osm_location->tiley = 0;                     
        osm_location->tilex_left_top_edge = 0;         
        osm_location->tiley_left_top_edge = 0;         
        osm_location->tilex_right_bottom_edge = 0;     
        osm_location->tiley_right_bottom_edge = 0;     
        osm_location->tilex_res = 0;                  
        osm_location->tiley_res = 0;                   
        osm_location->tilex_px_res = 0;                
        osm_location->tiley_px_res = 0;                
        osm_location->tilex_dest_px_res = 240;         
        osm_location->tiley_dest_px_res = 240;         
        osm_location->tilex_pos = 0;                 
        osm_location->tiley_pos = 0;    
        osm_location->tile_server_source_update = false;    
        osm_location->tile_server = NULL;
        osm_location->current_tile_url = NULL;
        osm_location->osm_map_data.header.always_zero = 0;
        osm_location->osm_map_data.header.cf = LV_IMG_CF_RAW_ALPHA;
        osm_location->osm_map_data.header.w = 256;
        osm_location->osm_map_data.header.h = 256;
        osm_location->osm_map_data.data = NULL;
        osm_location->osm_map_data.data_size = 0;
        for( int i = 0 ; i < DEFAULT_OSM_CACHE_SIZE ; i++ ) {
            osm_location->uri_load_dsc[ i ] = NULL;
        }
    }

    log_d("osm_location: alloc %d bytes at %p", sizeof( osm_location_t ), osm_location );

    return( osm_location );
}
/*
 * https://wiki.openstreetmap.org/wiki/Slippy_map_tilenames#C.2FC.2B.2B
 */
uint32_t osm_helper_long2tilex(double lon, uint32_t z){ 
	return (uint32_t)(floor((lon + 180.0) / 360.0 * (1 << z))); 
}

uint32_t osm_helper_lat2tiley(double lat, uint32_t z) { 
    double latrad = lat * M_PI/180.0;
	return (uint32_t)(floor((1.0 - asinh(tan(latrad)) / M_PI) / 2.0 * (1 << z))); 
}

double osm_helper_tilex2long(uint32_t x, uint32_t z) {
	return x / (double)(1 << z) * 360.0 - 180;
}

double osm_helper_tiley2lat(uint32_t y, uint32_t z) {
	double n = M_PI - 2.0 * M_PI * y / (double)(1 << z);
	return 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));
}

void osm_map_set_lon_lat( osm_location_t *osm_location, double lon, double lat ) {
    osm_location->lat = lat;
    osm_location->lon = lon;
}

uint32_t osm_map_get_zoom( osm_location_t *osm_location ) {
    return( osm_location->zoom );
}

void osm_map_zoom_in( osm_location_t *osm_location ) {
    if ( osm_location->zoom < 18 )
        osm_location->zoom++;
}
void osm_map_zoom_out( osm_location_t *osm_location ) {
    if ( osm_location->zoom > 2 )
        osm_location->zoom--;
}

void osm_map_set_zoom( osm_location_t *osm_location, uint32_t zoom ) {
    osm_location->zoom = zoom;
}

lv_img_dsc_t *osm_map_get_tile_image( osm_location_t *osm_location ) {
    if ( osm_location->osm_map_data.data ) {
        return( &osm_location->osm_map_data );
    }
    else {
        return( (lv_img_dsc_t*)&osm_no_data_240px );
    }
}

lv_img_dsc_t *osm_map_get_no_data_image( void ) {
    return( (lv_img_dsc_t*)&osm_no_data_240px );
}

bool osm_map_update( osm_location_t *osm_location ) {
    bool tile_update = false;
    /**
     * check if osm tile change
     */
    if ( osm_location->tilex != osm_helper_long2tilex( osm_location->lon, osm_location->zoom ) ) {
        osm_location->tilex = osm_helper_long2tilex( osm_location->lon, osm_location->zoom );
        tile_update = true;
    }
    if ( osm_location->tiley != osm_helper_lat2tiley( osm_location->lat, osm_location->zoom ) ) {
        osm_location->tiley = osm_helper_lat2tiley( osm_location->lat, osm_location->zoom );
        tile_update = true;
    }
    /**
     * if tile change, update tile image
     */
    if ( tile_update || osm_location->tile_server_source_update ) {
        osm_location->tile_server_source_update = false;
        tile_update = true;
        osm_location = osm_map_update_tile_image( osm_location );
    }
    /**
     * calculate new tile infomations
     */
    osm_location->tiley_left_top_edge = osm_helper_tiley2lat( osm_location->tiley, osm_location->zoom );
    osm_location->tilex_left_top_edge = osm_helper_tilex2long( osm_location->tilex, osm_location->zoom );
    osm_location->tiley_right_bottom_edge = osm_helper_tiley2lat( osm_location->tiley + 1, osm_location->zoom );
    osm_location->tilex_right_bottom_edge = osm_helper_tilex2long( osm_location->tilex + 1, osm_location->zoom );
    osm_location->tiley_res = abs( osm_helper_tiley2lat( osm_location->tiley, osm_location->zoom ) - osm_helper_tiley2lat( osm_location->tiley + 1, osm_location->zoom ) );
    osm_location->tilex_res = abs( osm_helper_tiley2lat( osm_location->tilex, osm_location->zoom ) - osm_helper_tiley2lat( osm_location->tilex + 1, osm_location->zoom ) );
    osm_location->tiley_px_res = osm_location->tiley_res / osm_location->tiley_dest_px_res;
    osm_location->tilex_px_res = osm_location->tilex_res / osm_location->tilex_dest_px_res;
    osm_location->tiley_pos = abs( osm_location->tiley_left_top_edge - osm_location->lat ) / osm_location->tiley_px_res;
    osm_location->tilex_pos = abs( osm_location->tilex_left_top_edge - osm_location->lon ) / osm_location->tilex_px_res;

    log_d("left/top: %f / %f", osm_location->tiley_left_top_edge, osm_location->tilex_left_top_edge );
    log_d("right/bottom: %f / %f", osm_location->tiley_right_bottom_edge, osm_location->tilex_right_bottom_edge );

    return( tile_update );
}

/**
 * @brief get an new tile from osm an store it in a lv_img_dsc structure for direct lv_img_set_src use
 * 
 * @param osm_location  pointer to the osm_location structure
 * 
 * @return  updated lv_img_dsc structure
 */
osm_location_t *osm_map_update_tile_image( osm_location_t *osm_location ) {
    uri_load_dsc_t *uri_load_dsc = NULL;
    /**
     * download file into RAM
     */
    uri_load_dsc = osm_map_get_cache_tile_image( osm_location );
    /**
     * check if download was success
     */
    if ( uri_load_dsc ) {
        /**
         * set new image data
         */
        osm_location->osm_map_data.data = uri_load_dsc->data;
        osm_location->osm_map_data.data_size = uri_load_dsc->size;
        lv_img_cache_invalidate_src( &osm_location->osm_map_data );
    }
    else {
        /**
         * set default no data image
         */
        osm_location->osm_map_data.data = osm_no_data_240px.data;
        osm_location->osm_map_data.data_size = osm_no_data_240px.data_size;
        lv_img_cache_invalidate_src( &osm_location->osm_map_data );
    }
    return( osm_location );
}

uri_load_dsc_t *osm_map_get_cache_tile_image( osm_location_t *osm_location ) {
    size_t tile = -1;
    size_t free_tile = -1;
    size_t cachesize = 0;
    size_t cachefile = 0;
    uint64_t timestamp = millis();
    uri_load_dsc_t *uri_load_dsc = NULL;
    /**
     * generate tile image utl/uri
     */
    osm_map_gen_url( osm_location );
    /**
     * check if tile image exist
     */
    for( int i = 0 ; i < DEFAULT_OSM_CACHE_SIZE ; i++ ) {
        if ( osm_location->uri_load_dsc[ i ] ) {
            if ( !strcmp( osm_location->current_tile_url, osm_location->uri_load_dsc[ i ]->uri ) ) {
                tile = i;
                break;
            }
        }
        else {
            free_tile = i;
        }
    }
    /**
     * check for a cache hit
     */
    if ( tile != -1 ) {
        log_i("url cache hit: %s", osm_location->uri_load_dsc[ tile ]->uri );
        uri_load_dsc = osm_location->uri_load_dsc[ tile ];
        uri_load_dsc->timestamp = millis();
    }
    else {
        /**
         * 1. check for a free tile
         */
        log_d("url cache miss for %s", osm_location->current_tile_url );
        if ( free_tile == -1 ) {
            /**
             * search the oldest one
             */
            for( int i = 0 ; i < DEFAULT_OSM_CACHE_SIZE ; i++ ) {
                if ( osm_location->uri_load_dsc[ i ]->timestamp <= timestamp && osm_location->uri_load_dsc[ i ] ) {
                    timestamp = osm_location->uri_load_dsc[ i ]->timestamp;
                    free_tile = i;
                }
            }
            /**
             * delete the oldest one
             */
            log_d("cache full, delete the oldest: %s", osm_location->uri_load_dsc[ free_tile ]->uri );
            uri_load_free_all( osm_location->uri_load_dsc[ free_tile ] );
            osm_location->uri_load_dsc[ free_tile ] = NULL;
        }
        log_d("use tile cache %d", free_tile );
        osm_location->uri_load_dsc[ free_tile ] = uri_load_to_ram( (const char*)osm_location->current_tile_url );
        uri_load_dsc = osm_location->uri_load_dsc[ free_tile ];
        /**
         * get cache size
         */
        for( int i = 0 ; i < DEFAULT_OSM_CACHE_SIZE ; i++ ) {
            if ( osm_location->uri_load_dsc[ i ] ) {
                cachesize += osm_location->uri_load_dsc[ i ]->size;
                cachefile++;
            }
        }
        log_i("cached files: %d, cachesize = %d bytes", cachefile, cachesize );
    }
    return( uri_load_dsc );
}

void osm_map_set_tile_server( osm_location_t *osm_location, const char* tile_server ) {
    /**
     * free old tile server entry
     */
    if( osm_location->tile_server ) {
        free( (void *)osm_location->tile_server );
        osm_location->tile_server = NULL;
    }
    /**
     * allocate new memory and copy new tile server
     */
    osm_location->tile_server = (char *)MALLOC( strlen( tile_server ) + 1 );
    if ( osm_location->tile_server ) {
        strcpy( osm_location->tile_server, tile_server );
        log_i("osm_location->tile_server: %s", osm_location->tile_server );
    }
    osm_location->tile_server_source_update = true;
}

void osm_map_gen_url( osm_location_t *osm_location ) {
    char *tile_server_p = NULL;
    char *current_tile_url_p = NULL;
    char temp_str[32] = "";
    char *temp_str_p = NULL;
    /**
     * is a tile server set?
     */
    if ( !osm_location->tile_server ) {
        log_i("set default osm tile server");
        osm_location->tile_server = (char*)MALLOC( sizeof( DEFAULT_OSM_TILE_SERVER ) );
        if ( !osm_location->tile_server ) {
            log_e("osm_location->tile_server: alloc failed");
            while(1);
        }
        log_d("osm_location->tile_server: alloc %d bytes at %p", sizeof( DEFAULT_OSM_TILE_SERVER ), osm_location->tile_server );
        strcpy( osm_location->tile_server, DEFAULT_OSM_TILE_SERVER );
    }
    /**
     * alloc current tile url
     */
    if ( !osm_location->current_tile_url ) {
        osm_location->current_tile_url = (char *)MALLOC( MAX_CURRENT_TILE_URL_LEN );
        if ( !osm_location->current_tile_url ) {
            log_e("osm_location->current_tile_url: alloc failed");
            while(1);
        }
        log_d("osm_location->current_tile_url: alloc %d bytes at %p", MAX_CURRENT_TILE_URL_LEN, osm_location->current_tile_url );
        *osm_location->current_tile_url = '\0';
    }
    /**
     * generate current tile url from tile server
     */
    tile_server_p = osm_location->tile_server;
    current_tile_url_p = osm_location->current_tile_url;

    while( *tile_server_p ) {
        if ( *tile_server_p == '$' ) {
            tile_server_p++;
            switch ( *tile_server_p ) {
                case 'z':
                    snprintf( temp_str, sizeof( temp_str ), "%d", osm_location->zoom );
                    break;
                case 'x':
                    snprintf( temp_str, sizeof( temp_str ), "%d", osm_location->tilex );
                    break;
                case 'y':
                    snprintf( temp_str, sizeof( temp_str ), "%d", osm_location->tiley );
                    break;
                default:
                    snprintf( temp_str, sizeof( temp_str ), "$%c", *tile_server_p );
                    break;
            }
            temp_str_p = temp_str;
            while( *temp_str_p ) {
                *current_tile_url_p = *temp_str_p;
                current_tile_url_p++;
                temp_str_p++;
            }
        }
        else {
            *current_tile_url_p = *tile_server_p;
            current_tile_url_p++;
        }
        tile_server_p++;
        *current_tile_url_p = '\0';
        if ( strlen( osm_location->current_tile_url ) >= MAX_CURRENT_TILE_URL_LEN ) {
            log_e("osm_location->current_tile_url: MAX_CURRENT_TILE_URL_LEN reached");
            *osm_location->current_tile_url = '\0';
            break;
        }
        log_d("current url: %s", osm_location->current_tile_url );
    }
    log_d("tile server: %s", osm_location->tile_server );
    log_d("current url: %s", osm_location->current_tile_url );
}