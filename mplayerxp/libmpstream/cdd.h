#ifndef __CDD_H__
#define __CDD_H__
#include <cdio/cdda.h>
#include "libmpconf/cfgparser.h"

namespace mpxp {
    struct libinput_t;

    struct cddb_data_t {
	std::string	cddb_hello;
	unsigned long	disc_id;
	unsigned int	tracks;
	char*		cache_dir;
	const char*	freedb_server;
	int		freedb_proto_level;
	int		anonymous;
	std::string	category;
	char*		xmcd_file;
	size_t		xmcd_file_size;
	any_t*		user_data;
	libinput_t*	libinput;
    };

    struct cd_toc_t {
	unsigned int min, sec, frame;
    };

    struct cd_track_t {
	std::string name;
	unsigned int track_nb;
	unsigned int min;
	unsigned int sec;
	unsigned int msec;
	unsigned long frame_begin;
	unsigned long frame_length;
	cd_track_t *prev;
	cd_track_t *next;
    };

    class CD_Info : public Opaque {
	public:
	    CD_Info();
	    virtual ~CD_Info();

	    virtual cd_track_t*	add_track(const char *track_name, unsigned int track_nb, unsigned int min, unsigned int sec, unsigned int msec, unsigned long frame_begin, unsigned long frame_length);
	    virtual cd_track_t*	get_track(unsigned int track_nb) const;

	    virtual void	print() const;
	    virtual MPXP_Rc	parse_xmcd(const char *_xmcd_file);

	    std::string artist;
	    std::string album;
	    std::string genre;
	private:
	    unsigned int nb_tracks;
	    unsigned int min;
	    unsigned int sec;
	    unsigned msec;

	    cd_track_t *first;
	    cd_track_t *last;
	    cd_track_t *current;
    };

    struct my_track_t {
	int play;
	lsn_t start_sector;
	lsn_t end_sector;
    };

    class CDD_Interface : public Opaque {
	public:
	    CDD_Interface();
	    virtual ~CDD_Interface();

	    virtual MPXP_Rc	open_cdda(const char* dev,const char* track);
	    virtual MPXP_Rc	open_cddb(libinput_t*,const char* dev,const char* track);

	    virtual int		read(char *buf,track_t* trackidx);
	    virtual void	seek(off_t pos,track_t *trackidx);
	    virtual off_t	tell() const;
	    virtual void	close();
	    virtual off_t	start() const;
	    virtual off_t	size() const;
	    virtual int		channels(unsigned track_idx) const;
	private:
	    lsn_t		map_sector(lsn_t sector,track_t *tr);
	    unsigned long	psa(unsigned long sector);

	    cdrom_drive_t* cd;
	    my_track_t tracks[256]; /* hope that's enough */
	    unsigned min;
	    unsigned sec;
	    unsigned msec;
	    lsn_t sector;
	    lsn_t start_sector;
	    lsn_t end_sector;
    };

    void cdda_register_options(m_config_t* cfg);
} // namespace mpxp
#endif // __CDD_H__
