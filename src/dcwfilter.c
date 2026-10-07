
inline int checksumDCW(uint8_t *data)
{
    if(data[3] != (byte)((data[0] + data[1] + data[2]) & 0xFF) || data[7] != (byte)((data[4] + data[5] + data[6]) & 0xFF)) {
      return 0;
    }
    if(data[11] != (byte)((data[8] + data[9] + data[10]) & 0xFF) || data[15] != (byte)((data[12] + data[13] + data[14]) & 0xFF)) {
      return 0;
    }
    return 1;
}


inline int isnullDCW(uint8_t *data)
{
	int a0 = ( !data[0] && !data[1] && !data[2] );
	int a1 = ( !data[4] && !data[5] && !data[6] );
	int b0 = ( !data[8] && !data[9] && !data[10] );
	int b1 = ( !data[12] && !data[13] && !data[14] );
	return ( (a0||a1) && (b0||b1) );
}

inline int isbadDCW(uint8_t *data)
{
	if ( data[0]!=0 && data[0]==data[1] && data[0]==data[2] ) return 1;
	if ( data[4]!=0 && data[4]==data[5] && data[4]==data[6] ) return 1;
	if ( data[8]!=0 && data[8]==data[9] && data[8]==data[10] ) return 1;
	if ( data[12]!=0 && data[12]==data[13] && data[12]==data[14] ) return 1;
	return 0;
}

int acceptDCW(uint8_t *data)
{

	if (!checksumDCW(data)) return 0;
	if ( isnullDCW(data) ) return 0;
	if ( isbadDCW(data) ) return 0;

	struct dcw_data *dcw = cfg.bad_dcw;
	while (dcw) {
		if (!memcmp(dcw->dcw, data, 16)) return 0;
		dcw = dcw->next;
	}

	return 1;
}

