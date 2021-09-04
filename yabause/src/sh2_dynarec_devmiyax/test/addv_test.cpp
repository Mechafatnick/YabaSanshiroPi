#include "gtest/gtest.h"
#include <core.h>
#include "sh2core.h"
#include "debug.h"
#include "yabause.h"
#include "memory_for_test.h"
#include "DynarecSh2.h"

namespace {

class AddvTest : public ::testing::Test {
 protected:
   DynarecSh2 * pctx_;

  AddvTest() {
    initMemory();
    pctx_ = new DynarecSh2();  
    pctx_->SetCurrentContext();
  }

  virtual ~AddvTest() {
    delete pctx_;    
  }   

virtual void SetUp() {
  
}

virtual void TearDown() {

}

};

TEST_F(AddvTest, normal) {

  pctx_->GetGenRegPtr()[2]=0x00000001; //source
  pctx_->GetGenRegPtr()[3]=0x00000001; //dest
  pctx_->SET_SR(0x0000000);

  // addv r3,r2
  memSetWord( 0x06000000, 0x332f );
  memSetWord( 0x06000002, 0x000b );  // rts
  memSetWord( 0x06000004, 0x0009 );  // nop

  pctx_->SET_PC( 0x06000000 );
  pctx_->Execute();

  EXPECT_EQ( 0x00000001, pctx_->GetGenRegPtr()[2] );
  EXPECT_EQ( 0x00000002, pctx_->GetGenRegPtr()[3] );
  EXPECT_EQ( 0x00000000, pctx_->GET_SR() );
}

TEST_F(AddvTest, normal_T1) {

   pctx_->GetGenRegPtr()[2]=0xFFFFFFFF;
   pctx_->GetGenRegPtr()[3]=0x00000001;
   pctx_->SET_SR(0x0000000);

   // addv r1,r2
   memSetWord( 0x06000000, 0x332f );
   memSetWord( 0x06000002, 0x000b );  // rts
   memSetWord( 0x06000004, 0x0009 );  // nop

   pctx_->SET_PC( 0x06000000 );
   pctx_->Execute();

   EXPECT_EQ( 0xffffffff, pctx_->GetGenRegPtr()[2] );
   EXPECT_EQ( 0x00000000, pctx_->GetGenRegPtr()[3] );
   EXPECT_EQ( 0x00000000, pctx_->GET_SR() );
}

TEST_F(AddvTest, normal_T21) {

   pctx_->GetGenRegPtr()[2]=0x7FFFFFFF;
   pctx_->GetGenRegPtr()[3]=0x00000002;
   pctx_->SET_SR(0x0000000);

   // subc r1,r2
   memSetWord( 0x06000000, 0x332f );
   memSetWord( 0x06000002, 0x000b );  // rts
   memSetWord( 0x06000004, 0x0009 );  // nop

   pctx_->SET_PC( 0x06000000 );
   pctx_->Execute();

   EXPECT_EQ( 0x7fffffff, pctx_->GetGenRegPtr()[2] );
   EXPECT_EQ( 0x80000001, pctx_->GetGenRegPtr()[3] );
   EXPECT_EQ( 0x00000001, pctx_->GET_SR() );
}

TEST_F(AddvTest, normal_T31) {

   pctx_->GetGenRegPtr()[2]=0x80000000;
   pctx_->GetGenRegPtr()[3]=0xFFFFFFFE;
   pctx_->SET_SR(0x0000000);

   // subc r1,r2
   memSetWord( 0x06000000, 0x332f );
   memSetWord( 0x06000002, 0x000b );  // rts
   memSetWord( 0x06000004, 0x0009 );  // nop

   pctx_->SET_PC( 0x06000000 );
   pctx_->Execute();

   EXPECT_EQ( 0x80000000, pctx_->GetGenRegPtr()[2] );
   EXPECT_EQ( 0x7ffffffe, pctx_->GetGenRegPtr()[3] );
   EXPECT_EQ( 0x00000001, pctx_->GET_SR() );
}

}  // namespacegPtr
