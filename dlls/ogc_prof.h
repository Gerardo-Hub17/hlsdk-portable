/*
 * Medicion de wii_prof para la IA: cada OGC_TIMED( "(ia) nombre" ) suma el tiempo de la funcion
 * a la tabla "servidor por entidad" del motor. Solo existe en la build de Wii; en el resto no hace nada.
 */
#ifndef OGC_PROF_H
#define OGC_PROF_H

#if XASH_OGC
extern "C" double Platform_DoubleTime( void );
extern "C" void OGC_EntProf( const char *classname, double dt );

struct COgcTimer
{
    const char *name;
    double t0;
    COgcTimer( const char *n ) : name( n ), t0( Platform_DoubleTime( )) {}
    ~COgcTimer() { OGC_EntProf( name, Platform_DoubleTime() - t0 ); }
};

#define OGC_TIMED( n ) COgcTimer ogc_timer_( n )
#else
#define OGC_TIMED( n )
#endif

#endif // OGC_PROF_H
