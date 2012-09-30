#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/errno.h>
#include <stdint.h>
#include <strings.h>
#include <stdlib.h>

#define RGB565
//#define RGB888

/* BLP file header */
typedef struct blp2header {
  uint8_t    ident[4];           // "BLP2" magic number
  uint32_t   type;               // Texture type: 0 = JPG, 1 = S3TC
  uint8_t    compression;        // Compression mode: 1 = raw, 2 = DXTC
  uint8_t    alpha_bits;         // 0, 1, 4, or 8
  uint8_t    alpha_type;         // 0, 1, 7, or 8
  uint8_t    has_mips;           // 0 = no mips levels, 1 = has mips (number
                                 // of levels determined by image size)
  uint32_t   width;              // Image width in pixels
  uint32_t   height;             // Image height in pixels
  uint32_t   mipmap_offsets[16]; // The file offsets of each mipmap, 0 for unused
  uint32_t   mipmap_lengths[16]; // The length of each mipmap data block
} blp2header;

typedef struct {
  uint16_t color0;
  uint16_t color1;
  uint32_t color_lookup;
} bc1_block_t;

typedef struct {
  uint64_t alpha;

  uint16_t color0;
  uint16_t color1;
  uint32_t color_lookup;
} bc2_block_t;

struct _alpha {
  uint8_t alpha0;
  uint8_t alpha1;
  uint8_t pad[6];
};

typedef struct {
  union {
    struct _alpha alpha;
    uint64_t alpha_lookup;
  };

  uint16_t color0;
  uint16_t color1;
  uint32_t color_lookup;
} bc3_block_t;

/* BMP file header */
typedef struct {
  uint16_t   id;
  uint32_t   size;
  uint16_t   reserved1;
  uint16_t   reserved2;
  uint32_t   offset;
} __attribute__((packed)) bmp_header_t;

/* BITMAPV4HEADER */
typedef struct {
  uint32_t  Size;            /* Size of this header in bytes */
  uint32_t  Width;           /* Image width in pixels */
  uint32_t  Height;          /* Image height in pixels */
  uint16_t  Planes;          /* Number of color planes */
  uint16_t  BitsPerPixel;    /* Number of bits per pixel */
  uint32_t  Compression;     /* Compression methods used */
  uint32_t  SizeOfBitmap;    /* Size of bitmap in bytes */
  uint32_t  HorzResolution;  /* Horizontal resolution in pixels per meter */
  uint32_t  VertResolution;  /* Vertical resolution in pixels per meter */
  uint32_t  ColorsUsed;      /* Number of colors in the image */
  uint32_t  ColorsImportant; /* Minimum number of important colors */
  /* Fields added for Windows 4.x follow this line */

  uint32_t  RedMask;       /* Mask identifying bits of red component */
  uint32_t  GreenMask;     /* Mask identifying bits of green component */
  uint32_t  BlueMask;      /* Mask identifying bits of blue component */
  uint32_t  AlphaMask;     /* Mask identifying bits of alpha component */
  uint32_t  CSType;        /* Color space type */
  uint32_t  RedX;          /* X coordinate of red endpoint */
  uint32_t  RedY;          /* Y coordinate of red endpoint */
  uint32_t  RedZ;          /* Z coordinate of red endpoint */
  uint32_t  GreenX;        /* X coordinate of green endpoint */
  uint32_t  GreenY;        /* Y coordinate of green endpoint */
  uint32_t  GreenZ;        /* Z coordinate of green endpoint */
  uint32_t  BlueX;         /* X coordinate of blue endpoint */
  uint32_t  BlueY;         /* Y coordiNate of blue endpoint */
  uint32_t  BlueZ;         /* Z coordinate of blue endpoint */
  uint32_t  GammaRed;      /* Gamma red coordinate scale value */
  uint32_t  GammaGreen;    /* Gamma green coordinate scale value */
  uint32_t  GammaBlue;     /* Gamma blue coordinate scale value */
} dib_header_t;

typedef struct {
  FILE         *fp;
  bmp_header_t bmp_header;
  dib_header_t dib_header;
  uint32_t     canvas[];
} __attribute__((packed)) bmp_info_t;

void bmp_add_pixel(bmp_info_t*, uint32_t, int, int);
int  bc1_decode_block(bmp_info_t*, bc1_block_t*, int, int);
int  bc2_decode_block(bmp_info_t*, bc2_block_t*, int, int);
int  bc3_decode_block(bmp_info_t*, bc3_block_t*, int, int);

bmp_info_t *
bmp_open(char *filename, int x, int y)
{
  bmp_info_t *bmp_info;
  bmp_header_t *bmp_header;
  dib_header_t *dib_header;

  bmp_info = (bmp_info_t *)malloc(sizeof(bmp_info_t) + 4 * x * y);
  if (bmp_info == NULL) {
    printf("%s: malloc: %s\n", filename, strerror(errno));
    //    perror("malloc");
    return NULL;
  }

  bzero(bmp_info, sizeof(bmp_info_t));

  bmp_info->fp = fopen(filename, "w+");
  if (bmp_info->fp == NULL) {
    printf("%s: fopen: %s\n", filename, strerror(errno));
    //perror("fopen");
    free(bmp_info);
    return NULL;
  }

  bmp_header = &bmp_info->bmp_header;
  dib_header = &bmp_info->dib_header;

  bmp_header->id     = 0x4d42; // "BM"
  bmp_header->size   = sizeof(bmp_header_t) + sizeof(dib_header_t) + 4 * x * y;
  bmp_header->offset = sizeof(bmp_header_t) + sizeof(dib_header_t);

  dib_header->Size            = sizeof(dib_header_t);
  dib_header->Width           = x;
  dib_header->Height          = y;
  dib_header->Planes          = 1;
  dib_header->BitsPerPixel    = 32;
  dib_header->Compression     = 3; // BI_BITFIELDS
  dib_header->SizeOfBitmap    = 4 * x * y;
  dib_header->HorzResolution  = 0x0b13; // 2835 pixels per meter
  dib_header->VertResolution  = 0x0b13;
  dib_header->ColorsUsed      = 0;
  dib_header->ColorsImportant = 0;

  // RGB565
  dib_header->RedMask         = 0x0000f800;
  dib_header->GreenMask       = 0x000007e0;
  dib_header->BlueMask        = 0x0000001f;
  dib_header->AlphaMask       = 0x00ff0000;

  dib_header->CSType          = 0x01; // Device dependent RGB
  
  return bmp_info;
}

void
bmp_add_pixel(bmp_info_t *bmp, uint32_t pixel, int x, int y)
{
  int width  = bmp->dib_header.Width;
  int height = bmp->dib_header.Height;
  uint32_t *canvas = bmp->canvas;

  /*
   * BMP pixels are stored starting in the lower left corner, going from left
   * to right, and then row by row from the bottom to the top of the image.
   */
  y = height - y -1;

  canvas[width * y + x] = pixel;
}

int
bmp_write(bmp_info_t *bmp)
{
  int rv;

  rv = fwrite(&bmp->bmp_header, bmp->bmp_header.size, 1, bmp->fp);
  if (rv < 1) {
    perror("fwrite");
    return -1;
  }

  return rv;
}

int
bmp_close(bmp_info_t *bmp)
{
  fclose(bmp->fp);
  free(bmp);
  return 0;
}

int
bmp_fill_from_bc1(bmp_info_t *bmp, bc1_block_t *bc1, int x, int y)
{
  int i, j;

  /* bc1 is a 4x4 pixels block */
  x /= 4;
  y /= 4;

  for (i=0; i<x; i++) {
    for (j=0; j<y; j++) {
      bc1_decode_block(bmp, &bc1[x*j + i], i*4, j*4);
    }
  }

  return 0;
}

int
bmp_fill_from_bc2(bmp_info_t *bmp, bc2_block_t *bc2, int x, int y)
{
  int i, j;

  /* bc2 is a 4x4 pixels block */
  x /= 4;
  y /= 4;

  for (i=0; i<x; i++) {
    for (j=0; j<y; j++) {
      bc2_decode_block(bmp, &bc2[x*j + i], i*4, j*4);
    }
  }

  return 0;
}

int
bmp_fill_from_bc3(bmp_info_t *bmp, bc3_block_t *bc3, int x, int y)
{
  int i, j;

  /* bc3 is a 4x4 pixels block */
  x /= 4;
  y /= 4;

  for (i=0; i<x; i++) {
    for (j=0; j<y; j++) {
      bc3_decode_block(bmp, &bc3[x*j + i], i*4, j*4);
    }
  }

  return 0;
}

void
blp2_dump_header(blp2header *header)
{
  int i;

  printf("%-14s %c%c%c%c\n", "Identity:", header->ident[0], header->ident[1],
	 header->ident[2], header->ident[3]);
  printf("%-14s %d (%s)\n", "Texture type:", header->type,
	 header->type?"S3TC":"JPG");
  printf("%-14s %d (%s)\n", "Compression:", header->compression,
	 header->compression==1?"RAW":"DXTC");
  printf("%-14s %d\n", "Alpha bits:", header->alpha_bits);
  printf("%-14s %d", "Alpha type:", header->alpha_type);
  switch (header->alpha_type) {
  case 0:
    printf(" (DXT1)\n");
    break;
  case 1:
    printf(" (DXT3)\n");
    break;
  case 7:
    printf(" (DXT5)\n");
    break;
  default:
    printf(" (Unspecified)\n");
    break;
  }
  printf("%-14s %d\n", "Width:", header->width);
  printf("%-14s %d\n", "Height:", header->height);

  printf("%-14s    | Offset   Length\n", "Mipmaps:");
  printf("%-14s ---+--------------------------\n", "");
  for (i=0; i<16; i++) {
    if (header->mipmap_offsets[i] !=0) {
      printf("%-14s %02d | %08x (%08x bytes)\n", "",
	     i, header->mipmap_offsets[i], header->mipmap_lengths[i]);
    } else {
      printf("%-14s %02d | unused\n", "", i);
    }
  }
}

int
bc1_decode_block(bmp_info_t *bmp, bc1_block_t *bc1, int x, int y)
{
  int i, j;
  int color_index;
  uint8_t  alpha;
  uint16_t color[4];
  uint32_t color_lookup;
  uint8_t r[2], g[2], b[2];

  color[0] = bc1->color0;
  color[1] = bc1->color1;

  // RGB565
  r[0] = (color[0] >> 11) & 0x1f;
  g[0] = (color[0] >> 5 ) & 0x3f;
  b[0] = (color[0] >> 0 ) & 0x1f;

  r[1] = (color[1] >> 11) & 0x1f;
  g[1] = (color[1] >> 5 ) & 0x3f;
  b[1] = (color[1] >> 0 ) & 0x1f;

  if (color[0] > color[1]) {
    color[2] = 
      (r[0]*2 + r[1]*1)/3 << 11 |
      (g[0]*2 + g[1]*1)/3 << 5  |
      (b[0]*2 + b[1]*1)/3 << 0;

    color[3] = 
      (r[0]*1 + r[1]*2)/3 << 11 |
      (g[0]*1 + g[1]*2)/3 << 5  |
      (b[0]*1 + b[1]*2)/3 << 0;
  } else {
    color[2] =
      (r[0] + r[1])/2 << 11 |
      (g[0] + g[1])/2 << 5  |
      (b[0] + b[1])/2 << 0;

    color[3] = 0;
  }

  color_lookup = bc1->color_lookup;

  for (i=0; i<4; i++) {    // row
    for (j=0; j<4; j++) {  // column
      color_index = color_lookup & 0x03;
      color_lookup >>= 2;

      if ((color[0] <= color[1]) && (color_index == 3)) {
	alpha = 0x00;
      } else {
	alpha = 0xff;
      }

      bmp_add_pixel(bmp, color[color_index]|(alpha<<16), x+j, y+i);
    }
  }

  return 0;
}

int
bc2_decode_block(bmp_info_t *bmp, bc2_block_t *bc2, int x, int y)
{
  int i, j;
  int color_index;
  uint8_t  alpha;
  uint16_t color[4];
  uint64_t alpha_lookup;
  uint32_t color_lookup;
  uint8_t r[2], g[2], b[2];

  color[0] = bc2->color0;
  color[1] = bc2->color1;

  // RGB565
  r[0] = (color[0] >> 11) & 0x1f;
  g[0] = (color[0] >> 5 ) & 0x3f;
  b[0] = (color[0] >> 0 ) & 0x1f;

  r[1] = (color[1] >> 11) & 0x1f;
  g[1] = (color[1] >> 5 ) & 0x3f;
  b[1] = (color[1] >> 0 ) & 0x1f;

  color[2] = 
    (r[0]*2 + r[1]*1)/3 << 11 |
    (g[0]*2 + g[1]*1)/3 << 5  |
    (b[0]*2 + b[1]*1)/3 << 0;

  color[3] = 
    (r[0]*1 + r[1]*2)/3 << 11 |
    (g[0]*1 + g[1]*2)/3 << 5  |
    (b[0]*1 + b[1]*2)/3 << 0;

  alpha_lookup = bc2->alpha;
  color_lookup = bc2->color_lookup;

  for (i=0; i<4; i++) {    // row
    for (j=0; j<4; j++) {  // column
      color_index = color_lookup & 0x03;
      color_lookup >>= 2;

      alpha = alpha_lookup & 0x0f;
      alpha |= alpha << 4; // scale to 8bits alpha
      alpha_lookup >>= 4;

      bmp_add_pixel(bmp, color[color_index]|(alpha<<16), x+j, y+i);
    }
  }

  return 0;
}

int
bc3_decode_block(bmp_info_t *bmp, bc3_block_t *bc3, int x, int y)
{
  int i, j;
  int alpha_index, color_index;
  uint8_t  alpha[8];
  uint16_t color[4];
  uint64_t alpha_lookup;
  uint32_t color_lookup;
  uint8_t r[2], g[2], b[2];

  alpha[0] = bc3->alpha.alpha0;
  alpha[1] = bc3->alpha.alpha1;

  if (alpha[0] > alpha[1]) {
    alpha[2] = (alpha[0]*6 + alpha[1]*1) / 7;
    alpha[3] = (alpha[0]*5 + alpha[1]*2) / 7;
    alpha[4] = (alpha[0]*4 + alpha[1]*3) / 7;
    alpha[5] = (alpha[0]*3 + alpha[1]*4) / 7;
    alpha[6] = (alpha[0]*2 + alpha[1]*5) / 7;
    alpha[7] = (alpha[0]*1 + alpha[1]*6) / 7;
  } else {
    alpha[2] = (alpha[0]*4 + alpha[1]*1) / 5;
    alpha[3] = (alpha[0]*3 + alpha[1]*2) / 5;
    alpha[4] = (alpha[0]*2 + alpha[1]*3) / 5;
    alpha[5] = (alpha[0]*1 + alpha[1]*4) / 5;
    alpha[6] = 0;
    alpha[7] = 255;
  }

  color[0] = bc3->color0;
  color[1] = bc3->color1;

  // RGB565
  r[0] = (color[0] >> 11) & 0x1f;
  g[0] = (color[0] >> 5 ) & 0x3f;
  b[0] = (color[0] >> 0 ) & 0x1f;

  r[1] = (color[1] >> 11) & 0x1f;
  g[1] = (color[1] >> 5 ) & 0x3f;
  b[1] = (color[1] >> 0 ) & 0x1f;

  color[2] = 
    (r[0]*2 + r[1]*1)/3 << 11 |
    (g[0]*2 + g[1]*1)/3 << 5  |
    (b[0]*2 + b[1]*1)/3 << 0;

  color[3] = 
    (r[0]*1 + r[1]*2)/3 << 11 |
    (g[0]*1 + g[1]*2)/3 << 5  |
    (b[0]*1 + b[1]*2)/3 << 0;

  color_lookup = bc3->color_lookup;
  alpha_lookup = bc3->alpha_lookup >> 16;

  for (i=0; i<4; i++) {    // row
    for (j=0; j<4; j++) {  // column
      color_index = color_lookup & 0x03;
      color_lookup >>= 2;

      alpha_index = alpha_lookup & 0x07;
      alpha_lookup >>=3;

      bmp_add_pixel(bmp, color[color_index]|(alpha[alpha_index]<<16), x+j, y+i);
    }
  }

  return 0;
}

unsigned int
numlen(uint32_t n)
{
  int i;

  for (i=1; n/=10; i++);

  return i;
}

void
usage(void)
{
  printf("Usage: blp2bmp [-i | -a] <blp_file>\n");
  printf("       -i      dump blp2 header\n");
  printf("       -a      convert all mipmaps\n");
}

int
main(int argc, char **argv)
{
  int fd;
  int i, rv;
  char *data;
  struct stat buf;
  blp2header *blp2;
  bmp_info_t *bmp;

  uint32_t width;
  uint32_t height;

  char *bmpfile;

  int dump_blp2_header = 0;
  int convert_all_mipmaps = 0;

  while ((rv = getopt(argc, argv, "ia")) != -1) {
    switch (rv) {
    case 'i':
      dump_blp2_header = 1;
      break;
    case 'a':
      convert_all_mipmaps = 1;
      break;
    case '?':
      printf("unknown option: -%c\n", optopt);
      break;
    default:
      usage();
      return -1;
    }
  }
  argc -= optind;
  argv += optind;

  if (argc != 1) {
    usage();
    return -1;
  }

  fd = open(argv[0], O_RDONLY);
  if (fd < 0) {
    perror("open");
    return -1;
  }

  rv = fstat(fd, &buf);
  if (rv < 0) {
    perror("fstat");
    close(fd);
    return -1;
  }

  data = mmap(NULL, buf.st_size, PROT_READ, MAP_FILE | MAP_PRIVATE, fd, 0);
  if (data == MAP_FAILED) {
    printf("%s: mmap: %s\n", argv[0], strerror(errno));
    //perror("mmap");
    close(fd);
    return -1;
  }

  blp2 = (blp2header *)data;

  if (dump_blp2_header) {
    blp2_dump_header(blp2);
  }

  width  = blp2->width;
  height = blp2->height;

  bmpfile = (char*)malloc(strlen(argv[0]) + numlen(width) + numlen(height) + 3);
  if (bmpfile == NULL) {
    perror("malloc");
    close(fd);
    return -1;
  }

  argv[0][strlen(argv[0])-4] = '\0';

  for (i=0; blp2->mipmap_offsets[i]!=0; i++) {
    width  = blp2->width / (1<<i);
    height = blp2->height / (1<<i);

    sprintf(bmpfile, "%s_%dx%d.bmp", argv[0], width, height);

    bmp = bmp_open(bmpfile, width, height);

    if (blp2->compression == 2) {
      switch (blp2->alpha_type) {
      case 0:
	bmp_fill_from_bc1(bmp, (bc1_block_t*)(data+blp2->mipmap_offsets[i]), width, height);
	break;
      case 1:
	bmp_fill_from_bc2(bmp, (bc2_block_t*)(data+blp2->mipmap_offsets[i]), width, height);
	break;
      case 7:
	bmp_fill_from_bc3(bmp, (bc3_block_t*)(data+blp2->mipmap_offsets[i]), width, height);
	break;
      default:
	printf("%s:unsupported compression type\n", argv[0]);
	break;
      }
    }

    bmp_write(bmp);
    bmp_close(bmp);

    if ((!convert_all_mipmaps) && (i == 0)) {
      break;
    }
  }

  rv = munmap(data, buf.st_size);
  if (rv < 0) {
    perror("munmap");
    free(bmpfile);
    close(fd);
    return -1;
  }

  free(bmpfile);
  close(fd);

  return 0;
}
