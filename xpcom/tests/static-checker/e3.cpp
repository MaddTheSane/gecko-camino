typedef int PRUint32;
typedef int PRInt32;

typedef PRUint32 nsresult;

nsresult foo(__attribute__((user("outparam"))) int *a) {
  *a = 0;
  return 1;
}
