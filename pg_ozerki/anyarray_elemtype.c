#include "postgres.h"
#include "utils/array.h"



PG_FUNCTION_INFO_V1(anyarray_elemtype);


Datum
anyarray_elemtype(PG_FUNCTION_ARGS)
{
	
	AnyArrayType *v = PG_GETARG_ANY_ARRAY_P(0);
	PG_RETURN_OID(AARR_ELEMTYPE(v));
	
}