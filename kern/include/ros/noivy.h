#ifndef NOIVY_H
#define NOIVY_H

#define BOUND(lo, hi)   
#define COUNT(n)        
#define SIZE(n)         
#define SAFE            
#define SNT             
#define DANGEROUS       

/* Begin Experimental attributes */
#define META(p)            
#define HANDLER_ATOMIC              
#define LOCK_HANDLER_ATOMIC(...)   
#define IN_HANDLER_ATOMIC  
#define IN_HANDLER         
#define ASYNC              
#define NORACE             
#define SYNCHRONOUS        
#define REGION(r)          
#define NOREGION           
#define SOMEREGION         
#define SAMEREGION         
#define DELETES_REGION(r)  
#ifdef ROS_KERNEL
# define GROUP(g)           
#endif
#define NOGROUP            
#define SOMEGROUP          
#define SAMEGROUP          
#define UNIQUE             
#define NOALIAS            
#define PAIRED_WITH(c)     
#define PAIRED(c1,c2)      
#define ARGPAIRED(c1,c2,arg) 
#define FNPTRCALLER(fn)    
#define INITSTRUCT(s)      
#define NOINIT             
#define WRITES(...)        
#define RPROTECT           
#define WPROTECT           
#define RWPROTECT          
#define R_PERMITTED(...) 
#define W_PERMITTED(...) 
#define RW_PERMITTED(...) 
/* End Experimental attributes */

#define BND(lo, hi)     
#define CT(n)           
#define SZ(n)           

#define EFAT            
#define FAT             

#define NULLTERM        
#define NT              
#define NTS             
#define NTC(n)          

#define NTDROPATTR      
#define NTEXPANDATTR    

#define NULLABLE
#define OPT             
#define NONNULL         

#define TRUSTED         
#define TRUSTEDBLOCK    

#define POLY           

#define COPYTYPE        

//specifies that Deputy's typechecker (but not optimizer) should assume
//that this lvalue is constant. (unsound)
#define ASSUMECONST     

#define WHEN(e)         

#define DMEMCPY(x, y, z) 
#define DMEMSET(x, y, z) 
#define DMEMCMP(x, y, z)

#define DALLOC(x)       
#define DREALLOC(x, y)  
#define DFREE(x)        

#define DVARARG(x)      
#define DPRINTF(x)      

#define NTDROP(x)       (x)
#define NTEXPAND(x)     (x)
#define TC(x)           (x)

#define TVATTR(x)       
#define TPATTR(x)       

#define TV(x)           void *
#define TP(x)           
#define NTP(n,x)
#define NTPTV(n)

/* Sharc Stuff */

#define SINTHREAD
#define SFNPTR
#define SHASDEF
#define SPURE
#define STCREATE(fn,arg)
#define SBARRIER(n)
#define SBARRIERFN

#define SLOCK(x)
#define SUNLOCK(x)
#define SLOCKED(l)
#define SSOMELOCK
#define SREADONLY
#define SRACY
#define SREADS(n)
#define SWRITES(n)
#define SREADSNT
#define SWRITESNT
#define SCTX
#define SPRIVATE
#define SDYNAMIC
#define SINDYNAMIC
#define SOUTDYNAMIC
#define SDYNBAR(b)

#define RO
#define LCKD(x)
#define RACY
#define PRIVATE

#define BASE(p)

#define SGROUP(g)
#define SNOGROUP
#define SRETGROUP
#define SARGGROUP

#define SSAME

#define SUNIQUE
#define SNOALIAS

#define SMAYCAST
#define SINMAYCAST
#define SOUTMAYCAST
#define SCONDMCRET

#define TRUSTEDBLOCK

#define SCAST(x) (x)
#define SINIT(x) (x)
#define SINIT_DOUBLE(x) (x)
#define SINIT_FLOAT(x) (x)

#define hs_nofree

#define DALLOC(x)
#define DFREE(x)


#endif // NOIVY_H

