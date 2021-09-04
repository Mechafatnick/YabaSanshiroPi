#include "gtest/gtest.h"
#include <core.h>
#include "sh2core.h"
#include "debug.h"
#include "yabause.h"
#include "memory_for_test.h"
#include "DynarecSh2.h"

namespace {

class SubcTest : public ::testing::Test {
 protected:
   DynarecSh2 * pctx_;

  SubcTest() {
    initMemory();
    pctx_ = new DynarecSh2();  
    pctx_->SetCurrentContext();
  }

  virtual ~SubcTest() {
    delete pctx_;    
  }   

virtual void SetUp() {
  
}

virtual void TearDown() {

}

};

TEST_F(SubcTest, normal) {

  pctx_->GetGenRegPtr()[1]=0x00000007;
  pctx_->GetGenRegPtr()[2]=0x00000000;
  pctx_->SET_SR(0x00000E0);

  // subc r1,r2
  memSetWord( 0x06000000, 0x312A );
  memSetWord( 0x06000002, 0x000b );  // rts
  memSetWord( 0x06000004, 0x0009 );  // nop

  pctx_->SET_PC( 0x06000000 );
  pctx_->Execute();

  EXPECT_EQ( 0x00000007, pctx_->GetGenRegPtr()[1] );
  EXPECT_EQ( 0x00000000, pctx_->GetGenRegPtr()[2] );
  EXPECT_EQ( 0x000000E0, pctx_->GET_SR() );
}

TEST_F(SubcTest, normal_T1) {

   pctx_->GetGenRegPtr()[1]=0x00000001;
   pctx_->GetGenRegPtr()[2]=0x00000002;
   pctx_->SET_SR(0x00000E0);

   // subc r1,r2
   memSetWord( 0x06000000, 0x312A );
   memSetWord( 0x06000002, 0x000b );  // rts
   memSetWord( 0x06000004, 0x0009 );  // nop

   pctx_->SET_PC( 0x06000000 );
   pctx_->Execute();

   EXPECT_EQ( 0xffffffff, pctx_->GetGenRegPtr()[1] );
   EXPECT_EQ( 0x00000002, pctx_->GetGenRegPtr()[2] );
   EXPECT_EQ( 0x000000E1, pctx_->GET_SR() );
}

TEST_F(SubcTest, normal_T21) {

   pctx_->GetGenRegPtr()[1]=0x00000001;
   pctx_->GetGenRegPtr()[2]=0x00000001;
   pctx_->SET_SR(0x00000E1);

   // subc r1,r2
   memSetWord( 0x06000000, 0x312A );
   memSetWord( 0x06000002, 0x000b );  // rts
   memSetWord( 0x06000004, 0x0009 );  // nop

   pctx_->SET_PC( 0x06000000 );
   pctx_->Execute();

   EXPECT_EQ( 0xffffffff, pctx_->GetGenRegPtr()[1] );
   EXPECT_EQ( 0x00000001, pctx_->GetGenRegPtr()[2] );
   EXPECT_EQ( 0x000000E1, pctx_->GET_SR() );
}

TEST_F(SubcTest, normal_T31) {

   pctx_->GetGenRegPtr()[1]=0x00000000;
   pctx_->GetGenRegPtr()[3]=0x00000001;
   pctx_->SET_SR(0x00000E0);

   // subc r1,r2
   memSetWord( 0x06000000, 0x313A );
   memSetWord( 0x06000002, 0x000b );  // rts
   memSetWord( 0x06000004, 0x0009 );  // nop

   pctx_->SET_PC( 0x06000000 );
   pctx_->Execute();

   EXPECT_EQ( 0xffffffff, pctx_->GetGenRegPtr()[1] );
   EXPECT_EQ( 0x00000001, pctx_->GetGenRegPtr()[3] );
   EXPECT_EQ( 0x000000E1, pctx_->GET_SR() );
}

TEST_F(SubcTest, normal_T32) {

   pctx_->GetGenRegPtr()[0]=0x00000000;
   pctx_->GetGenRegPtr()[2]=0x00000000;
   pctx_->SET_SR(0x00000E1);

   // subc r1,r2
   memSetWord( 0x06000000, 0x302A );
   memSetWord( 0x06000002, 0x000b );  // rts
   memSetWord( 0x06000004, 0x0009 );  // nop

   pctx_->SET_PC( 0x06000000 );
   pctx_->Execute();

   EXPECT_EQ( 0xffffffff, pctx_->GetGenRegPtr()[0] );
   EXPECT_EQ( 0x00000000, pctx_->GetGenRegPtr()[2] );
   EXPECT_EQ( 0x000000E1, pctx_->GET_SR() );
}

}  // namespacegPtr
