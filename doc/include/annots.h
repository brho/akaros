#ifndef ANNOT_H
#define ANNOT_H

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
#define GROUP(g)           
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

#define TV(x)           void * (x)
#define TP(x)           

#endif // ANNOT_H
